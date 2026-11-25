/** @file
 *  @brief Packet interface and parent templates keyed by PacketID.
 */

#ifndef SPXCORE_PACKET_PACKET_HELPER_HPP
#define SPXCORE_PACKET_PACKET_HELPER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "specialproxy/packet/PacketID.hpp"
#include "specialproxy/packet/PacketLimits.hpp"
#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/PacketVariant.hpp"
#include "specialproxy/packet/Payload.hpp"

namespace spx::packet {

/** Interface implemented by every typed packet. */
class IPacket : public std::enable_shared_from_this<IPacket> {
 public:
  virtual ~IPacket() = default;

  /** @brief Registry key of the most derived packet type. */
  [[nodiscard]] virtual auto id() const -> PacketID = 0;

  /** @brief ENet channel the packet is sent and received on. */
  [[nodiscard]] virtual auto channel() const -> int { return 0; }

  /** @brief Fill fields from a decoded payload.
   *
   * @param payload Payload whose type must match the packet kind.
   * @return True when the payload matched and all required fields were read.
   */
  virtual auto read(const Payload& payload) -> bool = 0;

  /** @brief Build the payload representing the current fields. */
  virtual auto write() -> Payload = 0;

  std::vector<std::byte> raw_data; ///< Original frame bytes when the packet was received.

  /** @brief True when raw_data holds the original frame. */
  [[nodiscard]] auto has_raw_data() const -> bool { return !raw_data.empty(); }
};

/** Parent for text packets, provides the identity and channel constants. */
template <PacketID Id, int Channel = 0>
struct TextPacket : IPacket {
  static constexpr PacketID ID{Id};             ///< Registry key of derived types.
  static constexpr int CHANNEL{Channel};        ///< ENet channel for derived types.
  static constexpr bool IsTextPacket = true;    ///< Compile time kind marker.

  [[nodiscard]] auto id() const -> PacketID override { return ID; }
  [[nodiscard]] auto channel() const -> int override { return CHANNEL; }
};

/** Parent for tank packets, owns the 56-byte header and extra data. */
template <PacketID Id, PacketType PktType, int Channel = 0>
struct GamePacket : IPacket {
  static constexpr PacketID ID{Id};             ///< Registry key of derived types.
  static constexpr int CHANNEL{Channel};        ///< ENet channel for derived types.
  static constexpr PacketType PACKET_TYPE{PktType}; ///< Tank sub-type for derived types.
  static constexpr bool IsGamePacket = true;    ///< Compile time kind marker.

  GameUpdatePacket game_packet{}; ///< Wire header written ahead of extra data.
  std::vector<std::byte> extra;   ///< Extra data appended after the header.

  [[nodiscard]] auto id() const -> PacketID override { return ID; }
  [[nodiscard]] auto channel() const -> int override { return CHANNEL; }
};

/** Parent for variant function calls, owns the header and parameter list. */
template <PacketID Id, int Channel = 0>
struct VariantPacket : IPacket {
  static constexpr PacketID ID{Id};             ///< Registry key of derived types.
  static constexpr int CHANNEL{Channel};        ///< ENet channel for derived types.
  static constexpr bool IsVariantPacket = true; ///< Compile time kind marker.

  GameUpdatePacket game_packet{}; ///< Wire header, defaults to the sender marker net id.
  VariantList list;               ///< Parameters, index 0 holds the function name.

  VariantPacket() {
    // The client ignores calls whose sender net id is not the marker -1.
    game_packet.net_id = 0xFFFFFFFFu;
  }

  /** @brief Overwrite index 0 with the wire name of the packet id. */
  void set_function_name() {
    if (Id != PacketID::Unknown) {
      list.set(0, VariantValue{std::string{packet_name(Id)}});
    }
  }

  [[nodiscard]] auto id() const -> PacketID override { return ID; }
  [[nodiscard]] auto channel() const -> int override { return CHANNEL; }
};

/** Serialization entry points shared by senders and the decoder. */
struct PacketHelper {
  /** @brief Serialize a packet through its payload.
   *
   * Packets holding raw_data send those bytes unchanged, otherwise the
   * payload returned by write() is serialized.
   *
   * @param packet Packet to serialize.
   * @return Frame bytes, or an empty vector when limits are exceeded.
   */
  [[nodiscard]] static auto serialize(IPacket& packet) -> std::vector<std::byte> {
    if (packet.has_raw_data()) {
      if (packet.raw_data.size() > limits::kMaxWirePacketBytes) {
        return {};
      }
      return packet.raw_data;
    }
    return serialize_payload(packet.write());
  }

  /** @brief Build a variant header with the proxy sender defaults.
   *
   * @return Header with net id -1 and no delay.
   */
  [[nodiscard]] static auto make_variant_header() -> GameUpdatePacket {
    GameUpdatePacket header{};
    header.net_id = 0xFFFFFFFFu;
    return header;
  }
};

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_HELPER_HPP
