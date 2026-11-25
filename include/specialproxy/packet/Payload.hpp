/** @file
 *  @brief Decoded payload types and wire serialization for a whole packet.
 */

#ifndef SPXCORE_PACKET_PAYLOAD_HPP
#define SPXCORE_PACKET_PAYLOAD_HPP

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "specialproxy/packet/PacketLimits.hpp"
#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/PacketVariant.hpp"
#include "specialproxy/utils/ByteStream.hpp"

namespace spx::packet {

/** Payload of a text packet, message types 1 through 8. */
struct TextPayload {
  uint32_t message_type{0}; ///< NetMessageType value carried in the frame prefix.
  std::string data;         ///< Raw text body without the frame prefix and without the trailing null.
};

/** Payload of a tank packet with its optional extra data. */
struct GamePayload {
  GameUpdatePacket packet{};  ///< 56-byte tank header copied from the wire.
  std::vector<std::byte> extra; ///< Bytes following the header, empty when not extended.
};

/** Payload of a variant function call tank packet. */
struct VariantPayload {
  GameUpdatePacket game_packet{}; ///< Tank header, type CallFunction with the extended flag set.
  VariantList list;               ///< Parsed parameter list, index 0 holds the function name.
};

/** Unrecognized frame preserved as raw bytes. */
struct RawPayload {
  std::vector<std::byte> data; ///< Complete frame bytes as received.
};

/** Any decodable packet payload. */
using Payload = std::variant<TextPayload, GamePayload, VariantPayload, RawPayload>;

/** @brief Typed read access to a payload alternative, null on mismatch. */
template <typename T>
[[nodiscard]] auto get_payload_if(const Payload& payload) -> const T* {
  return std::get_if<T>(&payload);
}

/** @brief View the bytes of a string as a byte span. */
[[nodiscard]] inline auto as_bytes(const std::string& text)
    -> std::span<const std::byte> {
  return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

/** @brief Serialize a payload into a complete ENet frame.
 *
 * Text frames carry the message type then the text plus one trailing null.
 * Game frames carry the message type then the tank header plus extra data.
 * Variant frames carry the message type, a CallFunction header, then the
 * serialized variant list.
 *
 * @param payload Payload to serialize.
 * @return Frame bytes, or an empty vector when a size limit is exceeded.
 */
[[nodiscard]] inline auto serialize_payload(const Payload& payload)
    -> std::vector<std::byte> {
  if (const auto* raw = get_payload_if<RawPayload>(payload)) {
    if (raw->data.size() > limits::kMaxWirePacketBytes) {
      return {};
    }
    return raw->data;
  }

  utils::ByteStream<uint32_t> stream;
  if (const auto* text = get_payload_if<TextPayload>(payload)) {
    stream.write(text->message_type);
    stream.write_data(as_bytes(text->data));
  } else if (const auto* game = get_payload_if<GamePayload>(payload)) {
    stream.write(static_cast<uint32_t>(NetMessageType::GamePacket));
    GameUpdatePacket header = game->packet;
    if (!game->extra.empty()) {
      if (game->extra.size() > limits::kMaxDeclaredExtraBytes) {
        return {};
      }
      header.flags |= PacketFlagExtended;
      header.data_size = static_cast<uint32_t>(game->extra.size());
    }
    stream.write(header);
    stream.write_data(game->extra);
  } else if (const auto* variant = get_payload_if<VariantPayload>(payload)) {
    stream.write(static_cast<uint32_t>(NetMessageType::GamePacket));
    const auto extra = variant->list.serialize();
    if (extra.size() > limits::kMaxDeclaredExtraBytes) {
      return {};
    }
    GameUpdatePacket header = variant->game_packet;
    header.type = static_cast<uint8_t>(PacketType::CallFunction);
    header.flags |= PacketFlagExtended;
    header.data_size = static_cast<uint32_t>(extra.size());
    stream.write(header);
    stream.write_data(extra);
  } else {
    return {};
  }

  std::vector<std::byte> frame = stream.take_data();
  if (frame.size() > limits::kMaxWirePacketBytes) {
    return {};
  }
  if (get_payload_if<TextPayload>(payload) != nullptr) {
    // Growtopia treats text bodies as null terminated strings.
    frame.push_back(std::byte{0});
  }
  return frame;
}

} // namespace spx::packet

#endif // SPXCORE_PACKET_PAYLOAD_HPP
