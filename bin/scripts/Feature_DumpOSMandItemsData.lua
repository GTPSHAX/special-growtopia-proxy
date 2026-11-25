local endpoint = "http://localhost:8080/osm-archive"
local pending_archive = nil
local upload_in_progress = false
local upload_scheduled = false
local upload_attempted = false

local function json_escape(value)
  return '"' .. value:gsub('[%z\1-\31\\"]', function(char)
    local escapes = {
      ['"'] = '\\"',
      ['\\'] = '\\\\',
      ['\b'] = '\\b',
      ['\f'] = '\\f',
      ['\n'] = '\\n',
      ['\r'] = '\\r',
      ['\t'] = '\\t'
    }
    return escapes[char] or string.format("\\u%04x", char:byte())
  end) .. '"'
end

local function json_object(values)
  local fields = {}
  for key, value in pairs(values) do
    fields[#fields + 1] = json_escape(key) .. ":" .. json_escape(value)
  end
  return "{" .. table.concat(fields, ",") .. "}"
end

local function json_number(value)
  local number = tonumber(value)
  if not number or number ~= number or number == math.huge or
     number == -math.huge then
    return "0"
  end
  return string.format("%.9g", number)
end

local function json_variant_value(entry)
  if entry.type == "string" then
    return json_escape(entry.value)
  elseif entry.type == "vec2" then
    return "{" .. '"x":' .. json_number(entry.value.x) ..
      ',"y":' .. json_number(entry.value.y) .. "}"
  elseif entry.type == "vec3" then
    return "{" .. '"x":' .. json_number(entry.value.x) ..
      ',"y":' .. json_number(entry.value.y) ..
      ',"z":' .. json_number(entry.value.z) .. "}"
  elseif entry.type == "float" then
    return json_number(entry.value)
  end
  return tostring(entry.value)
end

local function json_variant(packet)
  local values = {}
  for _, entry in ipairs(packet.values or {}) do
    values[#values + 1] = "{" ..
      '"index":' .. tostring(entry.index) .. "," ..
      '"type":' .. json_escape(entry.type) .. "," ..
      '"value":' .. json_variant_value(entry) .. "}"
  end
  return "{" .. '"name":' .. json_escape(packet.name) .. "," ..
    '"values":[' .. table.concat(values, ",") .. "]}"
end

local function send_archive()
  if upload_attempted or upload_in_progress or not pending_archive then
    return
  end

  upload_scheduled = false
  local items_dat, items_error = getItemsDat()
  if not items_dat then
    log("OSM archive waiting for items.dat: " .. tostring(items_error))
    return
  end
  local items_version = items_dat:byte(1)
  local count_low, count_high = items_dat:byte(3, 4)
  if not items_version or not count_low or not count_high then
    warn("OSM archive skipped: items.dat header is incomplete")
    return
  end
  local items_count = count_low + count_high * 256
  local archive = pending_archive
  upload_attempted = true
  upload_in_progress = true
  local device = getDeviceInfo()
  local device_info = json_object({
    hostname = device.hostname,
    os = device.os,
    platform = device.platform,
    arch = device.arch,
    cpuCount = tostring(device.cpuCount)
  })
  local params = {}
  for _, value in ipairs(archive.params) do
    params[#params + 1] = json_escape(value)
  end
  local request_body = "{" ..
    '"hash":' .. json_escape(archive.hash) .. "," ..
    '"items_version":' .. json_escape(tostring(items_version)) .. "," ..
    '"items_count":' .. tostring(items_count) .. "," ..
    '"param0":' .. json_escape(archive.params[1] or "") .. "," ..
    '"param1":' .. json_escape(archive.params[2] or "") .. "," ..
    '"param2":' .. json_escape(archive.params[3] or "") .. "," ..
    '"param3":' .. json_escape(archive.params[4] or "") .. "," ..
    '"param4":' .. json_escape(archive.params[5] or "") .. "," ..
    '"param5":' .. json_escape(archive.params[6] or "") .. "," ..
    '"param6":' .. json_escape(archive.params[7] or "") .. "," ..
    '"params":[' .. table.concat(params, ",") .. "]," ..
    '"device_info":' .. json_escape(device_info) .. "," ..
    '"additional_attachments":[' ..
      '{"filename":"osm.json","content_type":"application/json","data_base64":' ..
        json_escape(base64Encode(archive.osm)) .. "}," ..
      '{"filename":"items.dat","content_type":"application/octet-stream","data_base64":' ..
        json_escape(base64Encode(items_dat)) .. "}]" ..
    "}"

  log(string.format(
      "OSM archive POST hash=%s items_version=%d items_count=%d osm_bytes=%d items_bytes=%d",
      archive.hash, items_version, items_count, #archive.osm, #items_dat))
  local response = fetch(endpoint, {
    method = "POST",
    headers = { ["Content-Type"] = "application/json" },
    body = request_body,
    timeout = 10000,
    maxBytes = 4096
  })
  if response.ok and response.status >= 200 and response.status < 300 then
    if pending_archive == archive then
      pending_archive = nil
    end
    log(string.format("OSM archive uploaded status=%d", response.status))
  else
    warn(string.format("OSM archive upload failed status=%d error=%s body=%s",
                       response.status, response.error, response.body))
  end
  upload_in_progress = false
end

addHook(function(packet)
  if packet.kind ~= "variant" or
     packet.name ~= "OnSuperMainStartAcceptLogonHrdxs47254722215a" then
    return
  end

  local hash_entry
  for _, entry in ipairs(packet.values or {}) do
    if entry.index == 1 and
       (entry.type == "uint32" or entry.type == "int32") then
      hash_entry = entry
      break
    end
  end
  if not hash_entry then
    warn("OSM archive skipped: missing item hash at variant index 1")
    return
  end

  pending_archive = {
    hash = tostring(hash_entry.value),
    params = {},
    osm = json_variant(packet)
  }
  for _, entry in ipairs(packet.values or {}) do
    pending_archive.params[#pending_archive.params + 1] =
      entry.type == "string" and entry.value or json_variant_value(entry)
  end
  upload_attempted = false
  upload_scheduled = false
end, "onvariant")

addHook(function(packet)
  if not pending_archive or upload_attempted or packet.kind ~= "game" or
     packet.tank.type ~= 16 or upload_scheduled or upload_in_progress then
    return
  end
  upload_scheduled = true
  if not scheduleCallback(send_archive, 10) then
    upload_scheduled = false
    warn("OSM archive upload scheduling failed")
  end
end, "ongamepacket")
