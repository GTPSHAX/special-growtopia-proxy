#include "specialproxy/packet/PacketDecoder.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "specialproxy/packet/GenericPackets.hpp"
#include "specialproxy/packet/PacketID.hpp"
#include "specialproxy/packet/PacketLimits.hpp"
#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/PacketVariant.hpp"

namespace spx::packet {

auto PacketDecoder::parse_payload(std::span<const std::byte> frame)
    -> std::optional<Payload> {
  if (frame.size() < 4) {
    return std::nullopt;
  }
  uint32_t message_type = 0;
  std::memcpy(&message_type, frame.data(), 4);
  const auto body = frame.subspan(4);

  switch (message_type) {
  case static_cast<uint32_t>(NetMessageType::ServerHello):
  case static_cast<uint32_t>(NetMessageType::GenericText):
  case static_cast<uint32_t>(NetMessageType::GameMessage):
  case static_cast<uint32_t>(NetMessageType::Error):
  case static_cast<uint32_t>(NetMessageType::Track):
  case static_cast<uint32_t>(NetMessageType::ClientLogRequest):
  case static_cast<uint32_t>(NetMessageType::ClientLogResponse): {
    std::string text;
    // The final body byte is the terminator slot. Growtopia sometimes writes
    // a corrupt value there instead of null, so drop it unconditionally.
    if (body.size() > 1) {
      text.assign(reinterpret_cast<const char *>(body.data()), body.size() - 1);
      if (const auto terminator = text.find('\0');
          terminator != std::string::npos) {
        text.resize(terminator);
      }
    }
    return Payload{TextPayload{message_type, std::move(text)}};
  }

  case static_cast<uint32_t>(NetMessageType::GamePacket): {
    if (body.size() < sizeof(GameUpdatePacket)) {
      return std::nullopt;
    }
    GameUpdatePacket header{};
    std::memcpy(&header, body.data(), sizeof header);
    const auto extra = body.subspan(sizeof header);

    if ((header.flags & PacketFlagExtended) != 0u) {
      if (header.data_size > limits::kMaxDeclaredExtraBytes ||
          header.data_size > extra.size()) {
        return std::nullopt;
      }
      const auto payload_bytes = extra.first(header.data_size);
      if (header.type == static_cast<uint8_t>(PacketType::CallFunction)) {
        auto list = VariantList::parse(payload_bytes);
        if (!list) {
          return std::nullopt;
        }
        return Payload{VariantPayload{header, std::move(*list)}};
      }
      return Payload{
          GamePayload{header, {payload_bytes.begin(), payload_bytes.end()}}};
    }
    return Payload{GamePayload{header, {}}};
  }

  default: {
    if (frame.size() > limits::kMaxWirePacketBytes) {
      return std::nullopt;
    }
    return Payload{RawPayload{{frame.begin(), frame.end()}}};
  }
  }
}

auto PacketDecoder::decode(std::span<const std::byte> frame)
    -> std::optional<std::shared_ptr<IPacket>> {
  auto payload = parse_payload(frame);
  if (!payload) {
    return std::nullopt;
  }
  auto packet = PacketRegistry::instance().create(*payload);
  if (packet == nullptr) {
    // Unregistered identities fall back to generic carriers per payload kind.
    if (get_payload_if<TextPayload>(*payload) != nullptr) {
      packet = std::make_shared<GenericTextPacket>();
    } else if (get_payload_if<GamePayload>(*payload) != nullptr) {
      packet = std::make_shared<GenericGamePacket>();
    } else if (get_payload_if<VariantPayload>(*payload) != nullptr) {
      packet = std::make_shared<GenericVariantPacket>();
    } else {
      return std::nullopt;
    }
  }
  if (!packet->read(*payload)) {
    return std::nullopt;
  }
  packet->raw_data.assign(frame.begin(), frame.end());
  return packet;
}

auto PacketDecoder::encode(IPacket &packet) -> std::vector<std::byte> {
  return PacketHelper::serialize(packet);
}

} // namespace spx::packet
