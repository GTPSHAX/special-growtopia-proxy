/** @file
 *  @brief Shared size limits for wire packets and extended payloads.
 */

#ifndef SPXCORE_PACKET_PACKET_LIMITS_HPP
#define SPXCORE_PACKET_PACKET_LIMITS_HPP

#include <cstddef>
#include <cstdint>

#include "specialproxy/packet/PacketType.hpp"

namespace spx::packet::limits {

/** Maximum complete packet size accepted on the wire. */
inline constexpr size_t kMaxWirePacketBytes = size_t{10} * 1024 * 1024;

/** Maximum declared extra payload that fits in one wire packet. */
inline constexpr size_t kMaxDeclaredExtraBytes = [] {
  constexpr size_t kWireOverhead =
      sizeof(uint32_t) + sizeof(GameUpdatePacket) + 1;
  static_assert(kWireOverhead <= kMaxWirePacketBytes);
  return kMaxWirePacketBytes - kWireOverhead;
}();

} // namespace spx::packet::limits

#endif // SPXCORE_PACKET_PACKET_LIMITS_HPP
