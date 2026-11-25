/** @file
 *  @brief Wire frame parsing and typed packet decoding.
 */

#ifndef SPXCORE_PACKET_PACKET_DECODER_HPP
#define SPXCORE_PACKET_PACKET_DECODER_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "specialproxy/packet/PacketHelper.hpp"
#include "specialproxy/packet/PacketRegistry.hpp"
#include "specialproxy/packet/Payload.hpp"

namespace spx::packet {

/** Parsing and decoding entry points for complete wire frames. */
class PacketDecoder {
 public:
  /** @brief Parse one frame into a payload without creating a packet.
   *
   * Unknown message types and out of range tank sub-types come back as
   * RawPayload. All declared lengths are bounds checked before allocation.
   *
   * @param frame Complete wire bytes as received from ENet.
   * @return Parsed payload, or nullopt when the frame is malformed.
   */
  [[nodiscard]] static auto parse_payload(std::span<const std::byte> frame)
      -> std::optional<Payload>;

  /** @brief Parse one frame and decode it into a registered packet type.
   *
   * The payload identity selects the packet type from the shared registry,
   * then read() fills the fields. Identities without a registered packet
   * type decode into the matching generic packet so traffic is never
   * dropped. Malformed frames yield nullopt.
   *
   * @param frame Complete wire bytes as received from ENet.
   * @return Decoded packet, or nullopt when the frame is malformed.
   */
  [[nodiscard]] static auto decode(std::span<const std::byte> frame)
      -> std::optional<std::shared_ptr<IPacket>>;

  /** @brief Serialize a packet into a complete wire frame.
   *
   * @param packet Packet to encode.
   * @return Frame bytes, or an empty vector when limits are exceeded.
   */
  [[nodiscard]] static auto encode(IPacket& packet) -> std::vector<std::byte>;
};

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_DECODER_HPP
