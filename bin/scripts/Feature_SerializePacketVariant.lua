-- Logs every variant packet value together with its wire type.
-- Both directions are covered so the log shows client and server calls.

local function format_float(value)
  return string.format("%.6g", value)
end

local function format_value(type_name, value)
  if type_name == "string" then
    return string.format("%q", value)
  elseif type_name == "float" then
    return format_float(value)
  elseif type_name == "vec2" then
    return string.format("(%.3f, %.3f)", value.x, value.y)
  elseif type_name == "vec3" then
    return string.format("(%.3f, %.3f, %.3f)", value.x, value.y, value.z)
  elseif type_name == "uint32" or type_name == "int32" then
    return string.format("%d", value)
  end
  return tostring(value)
end

local function log_variant(direction, packet)
  if not packet or packet.kind ~= "variant" then
    return
  end
  local values = packet.values or {}
  local delay = 0
  if packet.tank then
    delay = packet.tank.int_data
  end
  log(string.format("[variant] %s session=%s name=%s delay=%d count=%d",
                    direction, tostring(packet.session), tostring(packet.name),
                    delay, #values))
  for _, entry in ipairs(values) do
    log(string.format("[variant]   [%d] %s %s", entry.index, entry.type,
                      format_value(entry.type, entry.value)))
  end
end

addHook(function(packet)
  log_variant("C2S", packet)
end, "onclientpacket")

addHook(function(packet)
  log_variant("S2C", packet)
end, "onserverpacket")
