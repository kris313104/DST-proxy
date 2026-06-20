// peer_proto.h — wire format for positions exchanged between companion apps over the
// WebRTC DataChannel. Reuses the same key=value encoding as the log parser.
#pragma once

#include <string>

#include "prox_parser.h"

namespace prox {

// Encode a local position update into a compact line for the DataChannel.
std::string encode_position(const Update& u);

// Decode a received line back into an Update. Returns false if nothing recognizable.
bool decode_position(const std::string& msg, Update& out);

}  // namespace prox
