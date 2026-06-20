// peer_proto.cpp — encode/decode peer positions for the DataChannel.
#include "peer_proto.h"

#include <cstdio>
#include <cstdlib>

namespace prox {

namespace {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

}  // namespace

std::string encode_position(const Update& u) {
    std::string s;
    s += "uid=" + quote(u.uid);
    s += " name=" + quote(u.name);
    char nums[160];
    std::snprintf(nums, sizeof(nums), " x=%.2f z=%.2f h=%.1f t=%.2f", u.x, u.z, u.heading, u.t);
    s += nums;
    return s;
}

bool decode_position(const std::string& msg, Update& out) {
    auto kv = parse_kv(msg);
    if (kv.find("uid") == kv.end() && kv.find("x") == kv.end()) return false;

    auto getd = [&](const char* k, double def) {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) return def;
        return std::strtod(it->second.c_str(), nullptr);
    };
    auto gets = [&](const char* k) {
        auto it = kv.find(k);
        return it == kv.end() ? std::string() : it->second;
    };

    out.uid = gets("uid");
    out.name = gets("name");
    out.x = getd("x", 0.0);
    out.z = getd("z", 0.0);
    out.heading = getd("h", 0.0);
    out.t = getd("t", 0.0);
    return true;
}

}  // namespace prox
