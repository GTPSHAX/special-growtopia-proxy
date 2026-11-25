-- Drops every tradeable inventory stack using prebuilt packet strings.
-- Untradeable items are skipped through the item definition flag.
-- The loop stops as soon as the server rejects a drop.

local stop_drop = false

addHook(function(session, body)
  if body and string.find(body, "cant_place_tile", 1, true) then
    stop_drop = true
  end
end, "ongamemessage")

registerCommand("/dropall", "Drop all tradeable items", function()
  local slots = getInventorySlots()
  local drops = {}
  local returns = {}
  local total = 0

  for i = 1, #slots do
    local item = slots[i]
    if item.id ~= 0 then
      local info = getItemInfo(item.id)
      if info and not info.untradeable then
        local id = tostring(item.id)
        total = total + 1
        drops[total] = "action|drop\n|itemID|" .. id
        returns[total] = "action|dialog_return\ndialog_name|drop_item\nitemID|" ..
                         id .. "|\ncount|" .. tostring(item.count)
      end
    end
  end

  if total == 0 then
    send_log("dropall nothing to drop")
    return
  end

  stop_drop = false
  blockDialog("drop_item", total)

  for i = 1, total do
    if stop_drop then break end
    sendPacket(2, drops[i])
    sleep(10)
    if stop_drop then break end
    sendPacket(2, returns[i])
    sleep(10)
  end

  send_log("dropall sent " .. tostring(total) .. " stacks")
end)
