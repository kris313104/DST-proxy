-- proximity_export.lua
-- Phase 0 core: every ~PERIOD seconds, print the LOCAL player's position to client_log.txt.
-- The companion app tails the log, parses [PROX] lines and drives proximity voice.
--
-- Loaded via modimport(), so it runs in the mod environment:
--  * game objects (ThePlayer, TheWorld, TheCamera, TheNet) live under GLOBAL,
--  * standard-library / engine globals (pcall, type, tostring, string, print, GetTime)
--    are NOT exposed directly in the sandbox — they must be pulled from GLOBAL,
--  * mod API functions (GetModConfigData, AddSimPostInit) are available directly.

local G = GLOBAL

-- Pull standard library functions from GLOBAL (the mod sandbox does not expose them directly).
local pcall = G.pcall
local type = G.type
local tostring = G.tostring
local string = G.string
local print = G.print

local PROTO_VERSION = 1

-- ---------------------------------------------------------------------------
-- Config
-- ---------------------------------------------------------------------------
local function cfg(name, default)
  local ok, v = pcall(GetModConfigData, name)
  if ok and v ~= nil then return v end
  return default
end

local update_hz = cfg("update_hz", 10)
if type(update_hz) ~= "number" or update_hz <= 0 then update_hz = 10 end
local PERIOD = 1 / update_hz

local DEBUG_IDS = cfg("debug_ids", true) and true or false
local DIAG_PERIOD = 5.0 -- seconds between ID-candidate diagnostics

-- In-game mute: a hotkey toggles a [PROX-CMD] mute line that the companion app honors,
-- so the player can mute/unmute their mic without alt-tabbing out of the game.
local mute_key_name = cfg("mute_key", "N")
local SHOW_INDICATOR = cfg("mute_indicator", true) and true or false
local MUTE_KEY_CODE = nil
if type(mute_key_name) == "string" and mute_key_name ~= "" then
  MUTE_KEY_CODE = G["KEY_" .. string.upper(mute_key_name)]
end

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local function safe(fn, default)
  local ok, res = pcall(fn)
  if ok and res ~= nil then return res end
  return default
end

-- Candidate identifiers for a shared session/world id (identical for all clients in
-- the same game). We cannot confirm which works without running the game, so we emit
-- several candidates; the 2-client test reveals which one is consistent.
local function id_candidates()
  local sid  = safe(function() return G.TheNet:GetSessionIdentifier() end, "")
  local sid2 = safe(function() return G.TheWorld and G.TheWorld.meta and G.TheWorld.meta.session_identifier end, "")
  local srv  = safe(function() return G.TheNet:GetServerName() end, "")
  return tostring(sid or ""), tostring(sid2 or ""), tostring(srv or "")
end

-- First non-empty candidate = current room identifier used for pairing.
local function pick_room(sid, sid2, srv)
  if sid ~= nil and sid ~= "" then return sid end
  if sid2 ~= nil and sid2 ~= "" then return sid2 end
  if srv ~= nil and srv ~= "" then return srv end
  return ""
end

-- True only during actual gameplay (not main menu / loading / after leaving).
-- Defaults to true if InGamePlay is unavailable, so exporting is never blocked by mistake.
local function in_gameplay()
  return safe(function() return G.InGamePlay() end, true) and true or false
end

-- ---------------------------------------------------------------------------
-- In-game mute (hotkey -> [PROX-CMD] line -> companion app mutes the mic)
-- ---------------------------------------------------------------------------
local muted = false
local indicator = nil  -- on-screen "MIC MUTED" Text widget (best-effort)

-- Emitting the command is the functional core: it MUST be simple and never fail.
local function emit_mute()
  print(string.format("[PROX-CMD] v=%d mute=%d", PROTO_VERSION, muted and 1 or 0))
end

-- Visual feedback only; entirely best-effort (any failure is swallowed).
local function update_indicator()
  if not SHOW_INDICATOR then return end
  pcall(function()
    local player = G.ThePlayer
    if player == nil or player.HUD == nil then return end
    if indicator == nil then
      local Text = G.require("widgets/text")
      indicator = player.HUD:AddChild(Text(G.DEFAULTFONT, 40))
      indicator:SetPosition(0, 250)
      indicator:SetColour(1, 0.35, 0.35, 1)
    end
    if muted then
      indicator:SetString("MIC MUTED")
      indicator:Show()
    else
      indicator:Hide()
    end
  end)
end

-- True if a chat/console text box is open, so the mute key isn't swallowed while typing.
local function text_input_open()
  local ok, res = pcall(function()
    local hud = G.ThePlayer and G.ThePlayer.HUD
    return hud ~= nil and hud.IsChatInputScreenOpen and hud:IsChatInputScreenOpen()
  end)
  return ok and res == true
end

local function toggle_mute()
  if not in_gameplay() then return end
  if text_input_open() then return end
  muted = not muted
  emit_mute()
  update_indicator()
end

-- Register the key handler exactly once (TheInput is global and survives world reloads).
local key_handler_installed = false
local function install_input()
  if key_handler_installed or MUTE_KEY_CODE == nil then return end
  local ok = pcall(function() G.TheInput:AddKeyDownHandler(MUTE_KEY_CODE, toggle_mute) end)
  if ok then key_handler_installed = true end
end
install_input()  -- try early; retried on world load in case TheInput wasn't ready yet

-- ---------------------------------------------------------------------------
-- Position export (per tick)
-- ---------------------------------------------------------------------------
local function export_tick()
  -- Don't export outside real gameplay (menu/loading/after leaving the world).
  if not in_gameplay() then return end

  local player = G.ThePlayer
  if player == nil or player.Transform == nil then return end

  -- Skip until the player is fully connected/spawned (otherwise we emit empty 0,0 frames).
  local uid = safe(function() return G.TheNet:GetUserID() end, "")
  if uid == nil or uid == "" then return end

  local x, _, z = player.Transform:GetWorldPosition()
  if x == nil or z == nil then return end

  local heading = safe(function() return G.TheCamera ~= nil and G.TheCamera:GetHeading() end, 0) or 0
  local name    = safe(function() return player:GetDisplayName() end, "")
  local t       = safe(function() return G.GetTime() end, 0) or 0
  local sid, sid2, srv = id_candidates()
  local room    = pick_room(sid, sid2, srv)

  -- One line, key=value; %q quotes and escapes string values (safe to parse).
  print(string.format(
    "[PROX] v=%d t=%.2f uid=%q name=%q x=%.2f z=%.2f h=%.1f room=%q",
    PROTO_VERSION, t, tostring(uid), tostring(name), x, z, heading, room))
end

-- ---------------------------------------------------------------------------
-- ID-candidate diagnostics (used to choose the room identifier in testing)
-- ---------------------------------------------------------------------------
local function diag_tick()
  if not in_gameplay() then return end
  local sid, sid2, srv = id_candidates()
  local room = pick_room(sid, sid2, srv)
  print(string.format(
    "[PROX-IDS] sid=%q sid2=%q srv=%q chosen_room=%q",
    sid, sid2, srv, room))
end

-- ---------------------------------------------------------------------------
-- Start: periodic tasks on the world entity (client). Re-registered on each world
-- load (AddSimPostInit), torn down when the world is destroyed.
-- ---------------------------------------------------------------------------
local announced = false
local function start_export()
  if G.TheWorld == nil then return end

  install_input()      -- ensure the mute hotkey is bound (no-op if already done)
  indicator = nil      -- the HUD was recreated on this world load; rebuild lazily
  pcall(update_indicator)

  G.TheWorld:DoPeriodicTask(PERIOD, export_tick)
  if DEBUG_IDS then
    diag_tick() -- emit once immediately
    G.TheWorld:DoPeriodicTask(DIAG_PERIOD, diag_tick)
  end

  if not announced then
    announced = true
    print(string.format(
      "[PROX] export started: period=%.3fs (%d Hz), proto=%d, debug_ids=%s",
      PERIOD, update_hz, PROTO_VERSION, tostring(DEBUG_IDS)))
  end
end

AddSimPostInit(start_export)
