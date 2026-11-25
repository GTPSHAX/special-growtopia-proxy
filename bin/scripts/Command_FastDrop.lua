-- When enabled, a single drop request from the
-- client is expanded into a dialog_return carrying the whole inventory stack.
-- The generated dialog is blocked so the client never sees the count prompt.

local enabled = false
local CHUNK_LIMIT = 200

local function parse_item_id(body)
  if not string.find(body, "action|drop", 1, true) then
    return nil
  end
  for raw_line in string.gmatch(body, "[^\n]+") do
    local line = raw_line
    if string.sub(line, 1, 1) == "|" then
      line = string.sub(line, 2)
    end
    if string.sub(line, 1, 7) == "itemID|" then
      local value = tonumber(string.sub(line, 8))
      if value and value > 0 then
        return math.floor(value)
      end
    end
  end
  return nil
end

local function inventory_count(id)
  for _, item in ipairs(getInventorySlots()) do
    if item.id == id then
      return item.count or 0
    end
  end
  return 0
end

local function send_chunk(id, count)
  blockDialog("drop_item", 1)
  sendPacket(2, "action|drop\n|itemID|" .. tostring(id))
  sendPacket(2, "action|dialog_return\ndialog_name|drop_item\nitemID|" ..
             tostring(id) .. "|\ncount|" .. tostring(count))
end

addHook(function(packet, raw)
  if not enabled or not packet or packet.kind ~= "text" then
    return true
  end
  local id = parse_item_id(packet.body or "")
  if not id then
    return true
  end
  local count = inventory_count(id)
  if count <= 0 then
    return true
  end
  local ok, info = pcall(getItemInfo, id)
  if ok and info and info.untradeable then
    return true
  end
  local remaining = count
  while remaining > 0 do
    local chunk = math.min(remaining, CHUNK_LIMIT)
    send_chunk(id, chunk)
    remaining = remaining - chunk
  end
  send_log("fastdrop item=" .. tostring(id) .. " count=" .. tostring(count))
  return false
end, "onclientpacket")

local function toggle()
  enabled = not enabled
  log("fastdrop " .. (enabled and "on" or "off"))
end

registerCommand("/fastdrop", "Toggle fast drop", function()
  toggle()
end)

registerCommand("/fd", "Toggle fast drop", function()
  toggle()
end)

registerCommand("/fdrop", "Toggle fast drop", function()
  toggle()
end)
