// voice_client.h — ties signaling + WebRTC mesh + audio + proximity mixing together.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "falloff.h"
#include "prox_parser.h"
#include "signaling_client.h"

namespace rtc {
class PeerConnection;
class DataChannel;
}

namespace prox {

class OpusCodec;
class AudioPreprocessor;

struct VoiceConfig {
    std::string signaling_url = "ws://127.0.0.1:8080";
    std::string join_token;     // optional shared secret if the server requires one
    std::string ca_bundle_file; // optional cacert.pem path to TLS-verify wss:// signaling
    std::vector<std::string> ice_servers = {"stun:stun.l.google.com:19302"};
    FalloffConfig falloff;
    int sample_rate = 48000;
    int frame_size = 960;
    int position_send_every = 5;     // send local position every N capture frames (~100ms)
    bool skip_far_audio = true;      // don't transmit audio to peers beyond audible range
    double gate_rms = 0.0;           // noise gate: skip frames quieter than this RMS (0..1); 0 = off
    bool denoise = true;             // speexdsp noise suppression on the mic
    bool agc = true;                 // speexdsp automatic gain control
    double pan_amount = 1.0;         // directional panning strength; 0 = off, negative = invert (calibration)
};

class VoiceClient {
public:
    explicit VoiceClient(VoiceConfig cfg);
    ~VoiceClient();

    void start(const std::string& room, const std::string& uid, const std::string& name);
    void stop();

    // Our own position (from the DST log or sim mode). Thread-safe.
    void setLocalPosition(const Update& u);

    // Push-to-talk gate: when false, mic audio is not transmitted. Thread-safe.
    void setTransmitting(bool on);

    // Wire to AudioIO: call from on_frame (mic) and on_render (speakers).
    void onCaptureFrame(const int16_t* mono, int frames);
    void render(float* stereo, int frames);

    int peerCount();
    int connectedCount();  // peers whose data channel is open
    std::string statsLine();

private:
    struct Peer;

    std::shared_ptr<Peer> getOrCreatePeer(const std::string& uid, const std::string& name,
                                          bool initiator);
    void removePeer(const std::string& uid);
    void wireDataChannel(const std::shared_ptr<Peer>& peer,
                         std::shared_ptr<rtc::DataChannel> dc);
    void handleSignal(const std::string& from, const SignalingClient::json& payload);

    VoiceConfig cfg_;
    std::unique_ptr<OpusCodec> encoder_;  // single shared encoder (we are one source)
    std::unique_ptr<AudioPreprocessor> preproc_;  // mic denoise/AGC (speexdsp)
    std::vector<int16_t> procbuf_;        // scratch for in-place preprocessing
    SignalingClient sig_;

    std::string myUid_;
    std::string myName_;
    std::string room_;

    std::mutex peersMtx_;
    std::map<std::string, std::shared_ptr<Peer>> peers_;

    std::mutex posMtx_;
    Update localPos_;

    std::mutex iceMtx_;
    std::vector<std::string> iceServers_;  // effective ICE servers (may be set by the server)

    int posSendCounter_ = 0;
    std::atomic<bool> txEnabled_{true};
    std::atomic<long> framesSent_{0};
    std::atomic<long> audioPktsRecv_{0};
    std::atomic<long> posRecv_{0};
};

}  // namespace prox
