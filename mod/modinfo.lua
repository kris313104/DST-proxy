-- Proximity Voice Export — client-only mod for Don't Starve Together.
-- Exports the local player's position (+ camera heading, userid, session id) to
-- client_log.txt, where a companion app reads it to drive proximity voice chat.
-- THE MOD ITSELF DOES NOT HANDLE VOICE — the DST Lua sandbox has no mic/network access.

name = "Proximity Voice Export"
description = [[Exports the local player's position to the log for an external proximity voice chat app.

NOTE: this mod does not play voice by itself. It requires a companion app (a separate program) that captures the microphone and plays audio with volume based on distance. The mod is client-only: it works on any server and the host does not need it.]]
author = "MegaMarchewa"
version = "0.1.0"

forumthread = ""

-- DST API
api_version = 10
dst_compatible = true
dont_starve_compatible = false
reign_of_giants_compatible = false
shipwrecked_compatible = false

-- Client side only; neither the server nor other players need the mod.
client_only_mod = true
all_clients_require_mod = false

server_filter_tags = {}

configuration_options = {
  {
    name = "update_hz",
    label = "Export rate",
    hover = "How many times per second to write the position to the log (higher = smoother, bigger log).",
    options = {
      { description = "5 Hz", data = 5 },
      { description = "10 Hz", data = 10 },
      { description = "15 Hz", data = 15 },
      { description = "20 Hz", data = 20 },
    },
    default = 10,
  },
  {
    name = "debug_ids",
    label = "Session ID diagnostics",
    hover = "Print candidate room identifiers (for the 2-client test). Turn off once chosen.",
    options = {
      { description = "On", data = true },
      { description = "Off", data = false },
    },
    default = true,
  },
}
