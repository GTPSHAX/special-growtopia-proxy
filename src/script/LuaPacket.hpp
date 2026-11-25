/** @file
 *  @brief Lua representation of one decoded proxy packet.
 */

#ifndef SPXCORE_SCRIPT_LUA_PACKET_HPP
#define SPXCORE_SCRIPT_LUA_PACKET_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/PacketVariant.hpp"

namespace spx::script {

/** Decoded fields copied from a frame before entering Lua. */
struct LuaPacketView {
  std::span<const std::byte> frame; ///< Complete wire bytes.
  std::string_view kind;            ///< text, game, variant or raw.
  uint32_t message_type{0};         ///< Frame message type.
  std::string body;                 ///< Text payload body.
  std::string variant_name;         ///< Variant function name.
  std::vector<std::optional<std::string>>
      params;                      ///< Variant parameters by index.
  packet::VariantList variants;    ///< Typed variant parameters by wire index.
  bool has_variants{false};        ///< Whether variants holds a parsed list.
  packet::GameUpdatePacket tank{}; ///< Tank header when applicable.
  uint32_t session_id{0};          ///< Owning session id.
};

/** Userdata carrying a tank header for the Mori style API. */
struct LuaTank {
  packet::GameUpdatePacket value{}; ///< Wire header copied into Lua ownership.
};

/** @brief Decode one frame into the fields exposed to Lua.
 *
 * @param[in] frame Complete wire bytes.
 * @param[in] session_id Owning session id.
 * @return View with decoded fields, kind "raw" for malformed frames.
 */
auto view_from_frame(std::span<const std::byte> frame, uint32_t session_id)
    -> LuaPacketView;

/** Push one decoded packet as a Lua table with a raw() method. */
void push_packet(lua_State *state, const LuaPacketView &view);

/** Push one tank header as userdata with named fields. */
void push_tank(lua_State *state, const packet::GameUpdatePacket &header);

/** @brief Borrow a tank userdata, null when the value has another type. */
auto get_tank(lua_State *state, int index) -> LuaTank *;

} // namespace spx::script

#endif // SPXCORE_SCRIPT_LUA_PACKET_HPP
