-- Example feature script loaded automatically from the scripts folder.
-- It uses both the Growlauncher style addHook and the Mori style addEvent.

addHook(function(packet, raw)
  if packet.kind == "text" and packet.body == "lua ping" then
    send_console("lua hook answered")
    return false
  end
end, "onclientpacket")

addEvent(Event.connect, function(session, address, port)
  log("lua connect event session " .. tostring(session))
end)

addEvent(Event.redirect, function(session, address, port)
  log("lua redirect target " .. address .. ":" .. tostring(port))
end)

registerCommand("/getinventory", "Send inventory to client chat", function(args)
  local slots = getInventorySlots()
  for index, item in ipairs(slots) do
    local name = item.empty and "(empty)" or item.name
    send_log(index .. ". " .. name .. " " .. tostring(item.count) .. "x")
  end
end)

addEvent(Event.inventory, function(session, changes)
  local slots = getInventorySlots()
  local empty = 0
  for _, item in ipairs(slots) do
    if item.empty then empty = empty + 1 end
  end
  if #changes > 16 then
    log("inventory sync session " .. tostring(session) .. " slots=" ..
        tostring(#slots) .. " empty=" .. tostring(empty) ..
        " changed=" .. tostring(#changes))
    return
  end
  for _, change in ipairs(changes) do
    local name = change.newId ~= 0 and change.name or change.oldName
    if name == "" then name = "(empty)" end
    local delta
    if change.oldId == change.newId then
      delta = change.newCount - change.oldCount
    elseif change.oldId == 0 then
      delta = change.newCount
    elseif change.newId == 0 then
      delta = -change.oldCount
    else
      delta = change.newCount
    end
    local sign = delta > 0 and "+" or ""
    send_log("inventory " .. sign .. tostring(delta) .. " " .. name ..
             " slot=" .. tostring(change.slot) .. " empty=" ..
             tostring(empty) .. "/" .. tostring(#slots))
  end
end)

registerCommand("/blockdialog", "Block matching dialogs", function(pattern, count)
  if pattern then
    blockDialog(pattern, tonumber(count) or 1)
  end
end)

registerCommand("/setslot", "Set one inventory slot", function(slot, id, count)
  local index = tonumber(slot)
  local item = tonumber(id)
  local amount = tonumber(count)
  if index and item and amount then
    setInventorySlot(index, item, amount)
  end
end)
