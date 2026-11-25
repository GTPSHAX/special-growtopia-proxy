-- Fast world switching. The command asks the server to leave the current
-- world, then answers the world select menu automatically without showing
-- that menu on the client.

local pending_world = nil

addHook(function(packet, raw)
  if not pending_world or not packet then
    return true
  end
  if packet.kind ~= "variant" then
    return true
  end
  if packet.name == "OnSetPos" then
    return false
  end
  if packet.name ~= "OnRequestWorldSelectMenu" then
    return true
  end
  local name = pending_world
  pending_world = nil
  sendPacket(3, "action|join_request\nname|" .. name .. "\ninvitedWorld|0")
  send_log("warp joining " .. name)
  return false
end, "onvariant")

registerCommand("/warp", "Warp to a world by name", function(name)
  if not name or name == "" then
    send_log("usage: /warp <world>")
    return
  end
  pending_world = name
  if not sendPacket(3, "action|quit_to_exit") then
    pending_world = nil
    send_log("warp failed to send quit_to_exit")
    return
  end
  send_log("warp leaving for " .. name)
end)
