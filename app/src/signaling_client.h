// signaling_client.h — WebSocket signaling client (libdatachannel rtc::WebSocket + JSON).
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rtc { class WebSocket; }

namespace prox {

struct PeerInfo {
    std::string uid;
    std::string name;
};

// Connects to the signaling server, joins a room, and surfaces room events.
// All callbacks are invoked from libdatachannel's internal threads.
class SignalingClient {
public:
    using json = nlohmann::json;

    std::function<void()> onOpen;
    // existing peers + ICE servers the server wants us to use (may be empty -> use local default)
    std::function<void(std::vector<PeerInfo>, std::vector<std::string>)> onWelcome;
    std::function<void(PeerInfo)> onPeerJoined;
    std::function<void(std::string uid)> onPeerLeft;
    std::function<void(std::string from, json payload)> onSignal;
    std::function<void()> onClosed;

    SignalingClient();
    ~SignalingClient();

    // Optional CA bundle file (cacert.pem) used to TLS-verify wss:// connections.
    void setCaFile(std::string path);

    void connect(const std::string& url, const std::string& room,
                 const std::string& uid, const std::string& name,
                 const std::string& token = "");
    void sendSignal(const std::string& to, const json& payload);
    void close();
    bool isOpen() const;

private:
    void handleMessage(const std::string& text);

    std::shared_ptr<rtc::WebSocket> ws_;
    std::string room_, uid_, name_, token_, caFile_;
    mutable std::mutex mtx_;
    bool open_ = false;
};

}  // namespace prox
