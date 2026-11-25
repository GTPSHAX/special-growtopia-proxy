#include "LuaPacket.hpp"

#include <cstring>
#include <string_view>

#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::script {

namespace {

constexpr const char *kTankMetatable = "spx.tank";

/** @brief Lua label for one variant wire type. */
auto variant_type_label(packet::VariantType type) -> const char * {
  switch (type) {
  case packet::VariantType::Float:
    return "float";
  case packet::VariantType::String:
    return "string";
  case packet::VariantType::Vec2:
    return "vec2";
  case packet::VariantType::Vec3:
    return "vec3";
  case packet::VariantType::UInt32:
    return "uint32";
  case packet::VariantType::Int32:
    return "int32";
  }
  return "unknown";
}

/** @brief Push one typed variant value onto the Lua stack. */
void push_variant_value(lua_State *state, const packet::VariantValue &value) {
  if (const auto *number = std::get_if<float>(&value)) {
    lua_pushnumber(state, static_cast<lua_Number>(*number));
  } else if (const auto *text = std::get_if<std::string>(&value)) {
    lua_pushlstring(state, text->data(), text->size());
  } else if (const auto *vec = std::get_if<packet::Vec2>(&value)) {
    lua_createtable(state, 0, 2);
    lua_pushnumber(state, static_cast<lua_Number>(vec->x));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(vec->y));
    lua_setfield(state, -2, "y");
  } else if (const auto *vec = std::get_if<packet::Vec3>(&value)) {
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, static_cast<lua_Number>(vec->x));
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, static_cast<lua_Number>(vec->y));
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, static_cast<lua_Number>(vec->z));
    lua_setfield(state, -2, "z");
  } else if (const auto *number = std::get_if<uint32_t>(&value)) {
    lua_pushinteger(state, static_cast<lua_Integer>(*number));
  } else if (const auto *number = std::get_if<int32_t>(&value)) {
    lua_pushinteger(state, static_cast<lua_Integer>(*number));
  } else {
    lua_pushnil(state);
  }
}

/** @brief Read one named field of a tank header. */
auto tank_get(lua_State *state, packet::GameUpdatePacket &header,
              std::string_view field) -> bool {
  if (field == "type")
    lua_pushinteger(state, header.type);
  else if (field == "net_id")
    lua_pushinteger(state, header.net_id);
  else if (field == "item_net_id")
    lua_pushinteger(state, header.item_net_id);
  else if (field == "flags")
    lua_pushinteger(state, header.flags);
  else if (field == "float_var")
    lua_pushnumber(state, header.float_var);
  else if (field == "int_data")
    lua_pushinteger(state, header.int_data);
  else if (field == "pos_x")
    lua_pushnumber(state, header.pos_x);
  else if (field == "pos_y")
    lua_pushnumber(state, header.pos_y);
  else if (field == "pos_x2")
    lua_pushnumber(state, header.pos_x2);
  else if (field == "pos_y2")
    lua_pushnumber(state, header.pos_y2);
  else if (field == "particle_time")
    lua_pushinteger(state, header.particle_time);
  else if (field == "int_x")
    lua_pushinteger(state, header.int_x);
  else if (field == "int_y")
    lua_pushinteger(state, header.int_y);
  else if (field == "data_size")
    lua_pushinteger(state, header.data_size);
  else
    return false;
  return true;
}

/** @brief Write one named field of a tank header. */
auto tank_set(lua_State *state, packet::GameUpdatePacket &header,
              std::string_view field) -> bool {
  const lua_Integer value = luaL_checkinteger(state, 3);
  if (field == "type")
    header.type = static_cast<uint8_t>(value);
  else if (field == "net_id")
    header.net_id = static_cast<uint32_t>(value);
  else if (field == "item_net_id")
    header.item_net_id = static_cast<int32_t>(value);
  else if (field == "flags")
    header.flags = static_cast<uint32_t>(value);
  else if (field == "float_var")
    header.float_var = static_cast<float>(luaL_checknumber(state, 3));
  else if (field == "int_data")
    header.int_data = static_cast<int32_t>(value);
  else if (field == "pos_x")
    header.pos_x = static_cast<float>(luaL_checknumber(state, 3));
  else if (field == "pos_y")
    header.pos_y = static_cast<float>(luaL_checknumber(state, 3));
  else if (field == "pos_x2")
    header.pos_x2 = static_cast<float>(luaL_checknumber(state, 3));
  else if (field == "pos_y2")
    header.pos_y2 = static_cast<float>(luaL_checknumber(state, 3));
  else if (field == "particle_time")
    header.particle_time = static_cast<uint32_t>(value);
  else if (field == "int_x")
    header.int_x = static_cast<int32_t>(value);
  else if (field == "int_y")
    header.int_y = static_cast<int32_t>(value);
  else if (field == "data_size")
    header.data_size = static_cast<uint32_t>(value);
  else
    return false;
  return true;
}

auto tank_index(lua_State *state) -> int {
  auto *tank =
      static_cast<LuaTank *>(luaL_checkudata(state, 1, kTankMetatable));
  const std::string_view field{luaL_checkstring(state, 2)};
  if (!tank_get(state, tank->value, field)) {
    lua_pushnil(state);
  }
  return 1;
}

auto tank_newindex(lua_State *state) -> int {
  auto *tank =
      static_cast<LuaTank *>(luaL_checkudata(state, 1, kTankMetatable));
  const std::string_view field{luaL_checkstring(state, 2)};
  if (!tank_set(state, tank->value, field)) {
    return luaL_error(state, "unknown tank field %s", field.data());
  }
  return 0;
}

auto packet_raw(lua_State *state) -> int {
  luaL_checktype(state, 1, LUA_TTABLE);
  lua_getfield(state, 1, "data");
  return 1;
}

} // namespace

void push_tank(lua_State *state, const packet::GameUpdatePacket &header) {
  auto *tank =
      static_cast<LuaTank *>(lua_newuserdatauv(state, sizeof(LuaTank), 0));
  tank->value = header;

  if (luaL_newmetatable(state, kTankMetatable) == 1) {
    lua_pushcfunction(state, tank_index);
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, tank_newindex);
    lua_setfield(state, -2, "__newindex");
  }
  lua_setmetatable(state, -2);
}

void push_packet(lua_State *state, const LuaPacketView &view) {
  lua_createtable(state, 0, 14);
  lua_pushlstring(state, reinterpret_cast<const char *>(view.frame.data()),
                  view.frame.size());
  lua_setfield(state, -2, "data");
  lua_pushinteger(state, static_cast<lua_Integer>(view.frame.size()));
  lua_setfield(state, -2, "size");
  lua_pushstring(state, std::string{view.kind}.c_str());
  lua_setfield(state, -2, "kind");
  lua_pushinteger(state, view.message_type);
  lua_setfield(state, -2, "message_type");
  lua_pushinteger(state, view.session_id);
  lua_setfield(state, -2, "session");

  if (!view.body.empty()) {
    lua_pushstring(state, view.body.c_str());
    lua_setfield(state, -2, "body");
  }
  if (!view.variant_name.empty()) {
    lua_pushstring(state, view.variant_name.c_str());
    lua_setfield(state, -2, "name");
    lua_createtable(state, 0, static_cast<int>(view.params.size()));
    for (size_t i = 0; i < view.params.size(); ++i) {
      if (!view.params[i]) {
        continue;
      }
      lua_pushinteger(state, static_cast<lua_Integer>(i + 1));
      lua_pushstring(state, view.params[i]->c_str());
      lua_settable(state, -3);
    }
    lua_setfield(state, -2, "params");
  }
  if (view.has_variants) {
    lua_createtable(state, 0, static_cast<int>(view.variants.size()));
    lua_Integer position = 0;
    for (size_t i = 0; i < packet::kMaxVariants; ++i) {
      const auto *value = view.variants.get(i);
      if (value == nullptr) {
        continue;
      }
      position += 1;
      lua_createtable(state, 0, 3);
      lua_pushinteger(state, static_cast<lua_Integer>(i));
      lua_setfield(state, -2, "index");
      lua_pushstring(state, variant_type_label(packet::wire_type(*value)));
      lua_setfield(state, -2, "type");
      push_variant_value(state, *value);
      lua_setfield(state, -2, "value");
      lua_rawseti(state, -2, position);
    }
    lua_setfield(state, -2, "values");
  }
  if (view.kind == "game" || view.kind == "variant") {
    push_tank(state, view.tank);
    lua_setfield(state, -2, "tank");
  }

  lua_pushcfunction(state, packet_raw);
  lua_setfield(state, -2, "raw");
}

auto get_tank(lua_State *state, int index) -> LuaTank * {
  return static_cast<LuaTank *>(luaL_testudata(state, index, kTankMetatable));
}

auto view_from_frame(std::span<const std::byte> frame, uint32_t session_id)
    -> LuaPacketView {
  LuaPacketView view;
  view.frame = frame;
  view.session_id = session_id;
  view.kind = "raw";
  auto payload = packet::PacketDecoder::parse_payload(frame);
  if (!payload) {
    return view;
  }
  if (const auto *text =
          packet::get_payload_if<packet::TextPayload>(*payload)) {
    view.kind = "text";
    view.message_type = text->message_type;
    view.body = text->data;
  } else if (const auto *game =
                 packet::get_payload_if<packet::GamePayload>(*payload)) {
    view.kind = "game";
    view.message_type =
        static_cast<uint32_t>(packet::NetMessageType::GamePacket);
    view.tank = game->packet;
  } else if (const auto *variant =
                 packet::get_payload_if<packet::VariantPayload>(*payload)) {
    view.kind = "variant";
    view.message_type =
        static_cast<uint32_t>(packet::NetMessageType::GamePacket);
    view.tank = variant->game_packet;
    view.variants = variant->list;
    view.has_variants = true;
    if (const auto *name = variant->list.get<std::string>(0)) {
      view.variant_name = *name;
    }
    for (size_t i = 1; i < packet::kMaxVariants; ++i) {
      if (const auto *value = variant->list.get(i)) {
        if (const auto *text_value = std::get_if<std::string>(value)) {
          view.params.push_back(*text_value);
        } else if (const auto *number = std::get_if<float>(value)) {
          view.params.push_back(std::to_string(*number));
        } else if (const auto *number = std::get_if<uint32_t>(value)) {
          view.params.push_back(std::to_string(*number));
        } else if (const auto *number = std::get_if<int32_t>(value)) {
          view.params.push_back(std::to_string(*number));
        } else {
          view.params.push_back(std::nullopt);
        }
      } else {
        view.params.push_back(std::nullopt);
      }
    }
  }
  return view;
}

} // namespace spx::script
