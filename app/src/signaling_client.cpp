// signaling_client.cpp — rtc::WebSocket-based signaling client.
#include "signaling_client.h"

#include <rtc/rtc.hpp>

#include <utility>
#include <variant>

namespace prox {

SignalingClient::SignalingClient() = default;
SignalingClient::~SignalingClient() { close(); }

void SignalingClient::setCaFile(std::string path) { caFile_ = std::move(path); }

void SignalingClient::connect(const std::string& url, const std::string& room,
                              const std::string& uid, const std::string& name,
                              const std::string& token) {
    room_ = room;
    uid_ = uid;
    name_ = name;
    token_ = token;

    // CA bundle (cacert.pem next to the exe) so wss:// is TLS-verified; ws:// ignores it.
    rtc::WebSocket::Configuration wcfg;
    if (!caFile_.empty()) wcfg.caCertificatePemFile = caFile_;
    ws_ = std::make_shared<rtc::WebSocket>(wcfg);

    ws_->onOpen([this]() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            open_ = true;
        }
        json j{{"type", "join"}, {"room", room_}, {"uid", uid_}, {"name", name_}};
        if (!token_.empty()) j["token"] = token_;
        std::shared_ptr<rtc::WebSocket> ws;
        { std::lock_guard<std::mutex> lk(mtx_); ws = ws_; }
        if (ws) ws->send(j.dump());
        if (onOpen) onOpen();
    });

    ws_->onClosed([this]() {
        { std::lock_guard<std::mutex> lk(mtx_); open_ = false; }
        if (onClosed) onClosed();
    });

    ws_->onError([](std::string /*e*/) {});

    ws_->onMessage([this](rtc::message_variant data) {
        if (std::holds_alternative<std::string>(data))
            handleMessage(std::get<std::string>(data));
    });

    ws_->open(url);
}

void SignalingClient::handleMessage(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (...) {
        return;
    }
    const std::string type = j.value("type", std::string());

    if (type == "welcome") {
        std::vector<PeerInfo> peers;
        for (auto& p : j.value("peers", json::array()))
            peers.push_back({p.value("uid", std::string()), p.value("name", std::string())});
        std::vector<std::string> ice;
        for (auto& s : j.value("iceServers", json::array()))
            if (s.is_string()) ice.push_back(s.get<std::string>());
        if (onWelcome) onWelcome(std::move(peers), std::move(ice));
    } else if (type == "peer-joined") {
        if (onPeerJoined) onPeerJoined({j.value("uid", std::string()), j.value("name", std::string())});
    } else if (type == "peer-left") {
        if (onPeerLeft) onPeerLeft(j.value("uid", std::string()));
    } else if (type == "signal") {
        if (onSignal) onSignal(j.value("from", std::string()), j.value("payload", json::object()));
    }
}

void SignalingClient::sendSignal(const std::string& to, const json& payload) {
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!open_) return;
        ws = ws_;
    }
    if (!ws) return;
    json j{{"type", "signal"}, {"to", to}, {"payload", payload}};
    ws->send(j.dump());
}

void SignalingClient::close() {
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ws = ws_;
        ws_.reset();
        open_ = false;
    }
    if (ws) {
        try { ws->close(); } catch (...) {}
    }
}

bool SignalingClient::isOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return open_;
}

}  // namespace prox
