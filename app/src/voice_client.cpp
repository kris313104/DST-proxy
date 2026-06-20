// voice_client.cpp — WebRTC mesh + audio + proximity mixing.
#include "voice_client.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <sstream>
#include <vector>

#include "audio_dsp.h"
#include "mixer.h"
#include "opus_codec.h"
#include "peer_proto.h"

namespace prox {

struct VoiceClient::Peer {
    std::string uid;
    std::string name;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;
    std::unique_ptr<OpusCodec> decoder;

    std::mutex jitterMtx;
    std::deque<int16_t> jitter;  // decoded mono samples awaiting playback

    std::mutex posMtx;
    Update pos;
    bool hasPos = false;

    std::atomic<bool> dcOpen{false};
};

VoiceClient::VoiceClient(VoiceConfig cfg) : cfg_(std::move(cfg)) {}
VoiceClient::~VoiceClient() { stop(); }

void VoiceClient::start(const std::string& room, const std::string& uid, const std::string& name) {
    myUid_ = uid;
    myName_ = name;
    room_ = room;
    if (cfg_.denoise || cfg_.agc) {
        preproc_ = std::make_unique<AudioPreprocessor>(cfg_.frame_size, cfg_.sample_rate,
                                                       cfg_.denoise, cfg_.agc);
        procbuf_.resize(static_cast<size_t>(cfg_.frame_size));
    }
    encoder_ = std::make_unique<OpusCodec>(cfg_.sample_rate, cfg_.frame_size);

    { std::lock_guard<std::mutex> lk(iceMtx_); iceServers_ = cfg_.ice_servers; }
    sig_.onWelcome = [this](std::vector<PeerInfo> peers, std::vector<std::string> ice) {
        if (!ice.empty()) {
            std::lock_guard<std::mutex> lk(iceMtx_);
            iceServers_ = std::move(ice);
        }
        for (auto& p : peers) getOrCreatePeer(p.uid, p.name, myUid_ < p.uid);
    };
    sig_.onPeerJoined = [this](PeerInfo p) {
        getOrCreatePeer(p.uid, p.name, myUid_ < p.uid);
    };
    sig_.onPeerLeft = [this](std::string uid) { removePeer(uid); };
    sig_.onSignal = [this](std::string from, SignalingClient::json payload) {
        handleSignal(from, payload);
    };

    sig_.setCaFile(cfg_.ca_bundle_file);
    sig_.connect(cfg_.signaling_url, room, uid, name, cfg_.join_token);
}

void VoiceClient::stop() {
    sig_.close();
    std::map<std::string, std::shared_ptr<Peer>> dead;
    {
        std::lock_guard<std::mutex> lk(peersMtx_);
        dead.swap(peers_);
    }
    dead.clear();  // closes peer connections
}

void VoiceClient::setLocalPosition(const Update& u) {
    std::lock_guard<std::mutex> lk(posMtx_);
    localPos_ = u;
}

void VoiceClient::setTransmitting(bool on) { txEnabled_.store(on); }

namespace {
double frame_rms(const int16_t* x, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = x[i] / 32768.0;
        s += v * v;
    }
    return std::sqrt(s / n);
}
}  // namespace

std::shared_ptr<VoiceClient::Peer> VoiceClient::getOrCreatePeer(const std::string& uid,
                                                               const std::string& name,
                                                               bool initiator) {
    std::lock_guard<std::mutex> lk(peersMtx_);
    auto it = peers_.find(uid);
    if (it != peers_.end()) return it->second;

    auto peer = std::make_shared<Peer>();
    peer->uid = uid;
    peer->name = name;
    peer->decoder = std::make_unique<OpusCodec>(cfg_.sample_rate, cfg_.frame_size);

    rtc::Configuration config;
    {
        std::lock_guard<std::mutex> lk(iceMtx_);
        for (auto& s : iceServers_) config.iceServers.emplace_back(s);
    }
    peer->pc = std::make_shared<rtc::PeerConnection>(config);

    peer->pc->onLocalDescription([this, uid](rtc::Description desc) {
        SignalingClient::json p{{"sdp", std::string(desc)}, {"sdptype", desc.typeString()}};
        sig_.sendSignal(uid, p);
    });
    peer->pc->onLocalCandidate([this, uid](rtc::Candidate c) {
        SignalingClient::json p{{"candidate", std::string(c)}, {"mid", c.mid()}};
        sig_.sendSignal(uid, p);
    });

    std::weak_ptr<Peer> wp = peer;
    if (initiator) {
        rtc::DataChannelInit init;
        init.reliability.unordered = true;
        init.reliability.maxRetransmits = 0;
        auto dc = peer->pc->createDataChannel("prox", init);
        wireDataChannel(peer, dc);
    } else {
        peer->pc->onDataChannel([this, wp](std::shared_ptr<rtc::DataChannel> dc) {
            if (auto p = wp.lock()) wireDataChannel(p, dc);
        });
    }

    peers_[uid] = peer;
    return peer;
}

void VoiceClient::wireDataChannel(const std::shared_ptr<Peer>& peer,
                                  std::shared_ptr<rtc::DataChannel> dc) {
    peer->dc = dc;
    std::weak_ptr<Peer> wp = peer;

    dc->onOpen([wp]() {
        if (auto p = wp.lock()) p->dcOpen = true;
    });
    dc->onClosed([wp]() {
        if (auto p = wp.lock()) p->dcOpen = false;
    });
    dc->onMessage([this, wp](rtc::message_variant msg) {
        auto p = wp.lock();
        if (!p) return;
        if (std::holds_alternative<rtc::binary>(msg)) {
            auto& bin = std::get<rtc::binary>(msg);
            std::vector<int16_t> pcm(static_cast<size_t>(cfg_.frame_size));
            int n = p->decoder->decode(reinterpret_cast<const unsigned char*>(bin.data()),
                                       bin.size(), pcm.data());
            if (n > 0) {
                std::lock_guard<std::mutex> lk(p->jitterMtx);
                for (int i = 0; i < n; ++i) p->jitter.push_back(pcm[i]);
                const size_t cap = static_cast<size_t>(cfg_.sample_rate) / 2;  // ~0.5s
                while (p->jitter.size() > cap) p->jitter.pop_front();
            }
            ++audioPktsRecv_;
        } else if (std::holds_alternative<std::string>(msg)) {
            Update u;
            if (decode_position(std::get<std::string>(msg), u)) {
                std::lock_guard<std::mutex> lk(p->posMtx);
                p->pos = u;
                p->hasPos = true;
            }
            ++posRecv_;
        }
    });
}

void VoiceClient::handleSignal(const std::string& from, const SignalingClient::json& payload) {
    auto peer = getOrCreatePeer(from, "", myUid_ < from);
    try {
        if (payload.contains("sdp")) {
            const std::string sdp = payload.value("sdp", std::string());
            const std::string type = payload.value("sdptype", std::string());
            peer->pc->setRemoteDescription(rtc::Description(sdp, type));
        } else if (payload.contains("candidate")) {
            const std::string cand = payload.value("candidate", std::string());
            const std::string mid = payload.value("mid", std::string());
            peer->pc->addRemoteCandidate(rtc::Candidate(cand, mid));
        }
    } catch (...) {
        // ignore malformed signaling
    }
}

void VoiceClient::removePeer(const std::string& uid) {
    std::shared_ptr<Peer> dead;
    {
        std::lock_guard<std::mutex> lk(peersMtx_);
        auto it = peers_.find(uid);
        if (it != peers_.end()) {
            dead = it->second;
            peers_.erase(it);
        }
    }
    // dead destroyed here, outside the lock, closing its PeerConnection.
}

void VoiceClient::onCaptureFrame(const int16_t* mono, int frames) {
    if (!encoder_ || frames != cfg_.frame_size) return;

    const int16_t* src = mono;
    if (preproc_ && preproc_->ok()) {
        procbuf_.assign(mono, mono + frames);
        preproc_->process(procbuf_.data());
        src = procbuf_.data();
    }

    bool tx = txEnabled_.load();
    if (tx && cfg_.gate_rms > 0.0 && frame_rms(src, frames) < cfg_.gate_rms) tx = false;

    Update lp;
    { std::lock_guard<std::mutex> lk(posMtx_); lp = localPos_; }

    // Gather open data channels, marking which peers are within audible range.
    struct Tgt { std::shared_ptr<rtc::DataChannel> dc; bool inRange; };
    std::vector<Tgt> targets;
    {
        std::lock_guard<std::mutex> lk(peersMtx_);
        for (auto& kv : peers_) {
            auto& p = kv.second;
            if (!p->dcOpen || !p->dc) continue;
            bool inRange = true;
            if (cfg_.skip_far_audio) {
                Update pp;
                bool hasPos;
                { std::lock_guard<std::mutex> lk2(p->posMtx); pp = p->pos; hasPos = p->hasPos; }
                if (hasPos) {
                    const double dx = pp.x - lp.x;
                    const double dz = pp.z - lp.z;
                    inRange = std::sqrt(dx * dx + dz * dz) <= cfg_.falloff.far_r * 1.1;
                }
            }
            targets.push_back({p->dc, inRange});
        }
    }

    if (tx) {
        auto pkt = encoder_->encode(src);
        if (!pkt.empty()) {
            rtc::binary b(reinterpret_cast<const std::byte*>(pkt.data()),
                          reinterpret_cast<const std::byte*>(pkt.data()) + pkt.size());
            bool sentAny = false;
            for (auto& t : targets) {
                if (!t.inRange) continue;
                try { t.dc->send(b); sentAny = true; } catch (...) {}
            }
            if (sentAny) ++framesSent_;
        }
    }

    if (++posSendCounter_ >= cfg_.position_send_every) {
        posSendCounter_ = 0;
        const std::string txt = encode_position(lp);
        for (auto& t : targets) {
            try { t.dc->send(txt); } catch (...) {}
        }
    }
}

void VoiceClient::render(float* stereo, int frames) {
    Update lp;
    { std::lock_guard<std::mutex> lk(posMtx_); lp = localPos_; }

    std::vector<std::shared_ptr<Peer>> plist;
    {
        std::lock_guard<std::mutex> lk(peersMtx_);
        for (auto& kv : peers_) plist.push_back(kv.second);
    }

    std::vector<int16_t> tmp(static_cast<size_t>(frames));
    for (auto& p : plist) {
        Update pp;
        bool hasPos;
        {
            std::lock_guard<std::mutex> lk(p->posMtx);
            pp = p->pos;
            hasPos = p->hasPos;
        }
        const double dx = pp.x - lp.x;
        const double dz = pp.z - lp.z;
        const double gain = hasPos ? gain_for_delta(dx, dz, cfg_.falloff) : 1.0;
        double pan = hasPos ? stereo_pan(dx, dz, lp.heading) : 0.0;
        pan = std::clamp(pan * cfg_.pan_amount, -1.0, 1.0);

        {
            std::lock_guard<std::mutex> lk(p->jitterMtx);
            for (int i = 0; i < frames; ++i) {
                if (!p->jitter.empty()) {
                    tmp[i] = p->jitter.front();
                    p->jitter.pop_front();
                } else {
                    tmp[i] = 0;
                }
            }
        }
        if (gain > 0.0) mix_mono_into_stereo(stereo, static_cast<size_t>(frames), tmp.data(), gain, pan);
    }
}

int VoiceClient::peerCount() {
    std::lock_guard<std::mutex> lk(peersMtx_);
    return static_cast<int>(peers_.size());
}

int VoiceClient::connectedCount() {
    std::lock_guard<std::mutex> lk(peersMtx_);
    int c = 0;
    for (auto& kv : peers_)
        if (kv.second->dcOpen) ++c;
    return c;
}

std::string VoiceClient::statsLine() {
    int connected = 0;
    int total = 0;
    {
        std::lock_guard<std::mutex> lk(peersMtx_);
        total = static_cast<int>(peers_.size());
        for (auto& kv : peers_)
            if (kv.second->dcOpen) ++connected;
    }
    std::ostringstream os;
    os << "peers=" << connected << "/" << total << " sent=" << framesSent_.load()
       << " audioRecv=" << audioPktsRecv_.load() << " posRecv=" << posRecv_.load();
    return os.str();
}

}  // namespace prox
