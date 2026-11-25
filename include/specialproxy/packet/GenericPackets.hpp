/** @file
 *  @brief Untyped fallback packets for unknown or pass-through traffic.
 */

#ifndef SPXCORE_PACKET_GENERIC_PACKETS_HPP
#define SPXCORE_PACKET_GENERIC_PACKETS_HPP

#include "specialproxy/packet/PacketHelper.hpp"
#include "specialproxy/packet/PacketID.hpp"
#include "specialproxy/packet/PacketType.hpp"

namespace spx::packet {

/** Accepts any text payload and keeps its message type and body. */
struct GenericTextPacket : TextPacket<PacketID::Unknown> {
  uint32_t message_type{static_cast<uint32_t>(NetMessageType::GameMessage)};
  std::string text;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<TextPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    message_type = parsed->message_type;
    text = parsed->data;
    return true;
  }

  auto write() -> Payload override {
    return TextPayload{message_type, text};
  }
};

/** Accepts any tank payload and keeps its header and extra data. */
struct GenericGamePacket : GamePacket<PacketID::Unknown, PacketType::State> {
  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<GamePayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->packet;
    extra = parsed->extra;
    return true;
  }

  auto write() -> Payload override {
    return GamePayload{game_packet, extra};
  }
};

/** Accepts any variant payload and keeps its header and parameter list. */
struct GenericVariantPacket : VariantPacket<PacketID::Unknown> {
  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    return true;
  }

  auto write() -> Payload override {
    return VariantPayload{game_packet, list};
  }
};

} // namespace spx::packet

#endif // SPXCORE_PACKET_GENERIC_PACKETS_HPP
