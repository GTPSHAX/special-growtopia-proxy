-- Toggles the world gravity effect used by the vanilla Antigravity Generator.
-- Both packets go downstream to the game client, which owns the effect.
-- The generator is placed and then damaged at the fixed tile that carries it.

local enabled = false

local TILE_X = 98
local TILE_Y = 55
local ITEM_ANTIGRAVITY_GENERATOR = 4992
local PACKET_TILE_CHANGE_REQUEST = 3
local PACKET_TILE_APPLY_DAMAGE = 8

local function send_gravity_packets()
  local place = GameUpdatePacket.new()
  place.type = PACKET_TILE_CHANGE_REQUEST
  place.int_data = ITEM_ANTIGRAVITY_GENERATOR
  place.int_x = TILE_X
  place.int_y = TILE_Y
  if not sendRawToClient(place) then
    return false
  end

  local damage = GameUpdatePacket.new()
  damage.type = PACKET_TILE_APPLY_DAMAGE
  damage.int_x = TILE_X
  damage.int_y = TILE_Y
  return sendRawToClient(damage)
end

local function toggle()
  if not send_gravity_packets() then
    log("antigravity packet send failed")
    return
  end
  enabled = not enabled
  log("antigravity " .. (enabled and "on" or "off"))
end

registerCommand("/ag", "Toggle world antigravity", function()
  toggle()
end)

registerCommand("/antigravity", "Toggle world antigravity", function()
  toggle()
end)
