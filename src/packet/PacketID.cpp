#include "specialproxy/packet/PacketID.hpp"

#include <array>
#include <string_view>

#include "specialproxy/packet/Payload.hpp"

namespace spx::packet {

namespace {

// Indexed by PacketID, order must match the enum declaration.
constexpr std::array<std::string_view, static_cast<size_t>(PacketID::Count)>
    kPacketNames{
        "Unknown",
        "ServerHello",
        "Quit",
        "QuitToExit",
        "JoinRequest",
        "ValidateWorld",
        "Input",
        "Log",
        "RefreshItemData",
        "Disconnect",
        "TileChangeRequest",
        "SendMapData",
        "SendTileUpdateData",
        "SendTileUpdateDataMultiple",
        "SendItemDatabaseData",
        "SendInventoryState",
        "ModifyItemInventory",
        "ItemChangeObject",
        "OnConsoleMessage",
        "OnTextOverlay",
        "OnDialogRequest",
        "OnTalkBubble",
        "OnNameChanged",
        "OnRequestAuctionPage",
        "OnSendToServer",
        "OnSpawn",
        "OnSuperMainStartAcceptLogonHrdxs47254722215a",
};

struct TextActionEntry {
  std::string_view action;
  PacketID id;
};

// Matched against the first pipe field after the action| prefix.
constexpr std::array<TextActionEntry, 7> kTextActions{{
    {"quit", PacketID::Quit},
    {"quit_to_exit", PacketID::QuitToExit},
    {"join_request", PacketID::JoinRequest},
    {"validate_world", PacketID::ValidateWorld},
    {"input", PacketID::Input},
    {"log", PacketID::Log},
    {"refresh_item_data", PacketID::RefreshItemData},
}};

constexpr std::string_view kActionPrefix{"action|"};

} // namespace

static_assert(kPacketNames[static_cast<size_t>(PacketID::Unknown)] ==
              "Unknown");

auto packet_name(PacketID id) -> std::string_view {
  const auto index = static_cast<size_t>(id);
  return index < kPacketNames.size() ? kPacketNames[index] : kPacketNames[0];
}

auto packet_id_from_name(std::string_view name) -> std::optional<PacketID> {
  for (size_t i = 1; i < kPacketNames.size(); ++i) {
    if (kPacketNames[i] == name) {
      return static_cast<PacketID>(i);
    }
  }
  return std::nullopt;
}

auto derive_packet_id(const TextPayload &payload) -> PacketID {
  if (payload.message_type == 1u) {
    return PacketID::ServerHello;
  }
  std::string_view line{payload.data};
  const size_t line_end = line.find('\n');
  if (line_end != std::string_view::npos) {
    line = line.substr(0, line_end);
  }
  if (!line.starts_with(kActionPrefix)) {
    return PacketID::Unknown;
  }
  std::string_view rest = line.substr(kActionPrefix.size());
  const size_t field_end = rest.find('|');
  if (field_end != std::string_view::npos) {
    rest = rest.substr(0, field_end);
  }
  for (const auto &entry : kTextActions) {
    if (entry.action == rest) {
      return entry.id;
    }
  }
  return PacketID::Unknown;
}

auto derive_packet_id(const GamePayload &payload) -> PacketID {
  switch (static_cast<PacketType>(payload.packet.type)) {
  case PacketType::Disconnect:
    return PacketID::Disconnect;
  case PacketType::TileChangeRequest:
    return PacketID::TileChangeRequest;
  case PacketType::SendMapData:
    return PacketID::SendMapData;
  case PacketType::SendTileUpdateData:
    return PacketID::SendTileUpdateData;
  case PacketType::SendTileUpdateDataMultiple:
    return PacketID::SendTileUpdateDataMultiple;
  case PacketType::SendItemDatabaseData:
    return PacketID::SendItemDatabaseData;
  case PacketType::SendInventoryState:
    return PacketID::SendInventoryState;
  case PacketType::ModifyItemInventory:
    return PacketID::ModifyItemInventory;
  case PacketType::ItemChangeObject:
    return PacketID::ItemChangeObject;
  default:
    return PacketID::Unknown;
  }
}

auto derive_packet_id(const VariantPayload &payload) -> PacketID {
  const VariantValue *name = payload.list.get(0);
  if (name == nullptr) {
    return PacketID::Unknown;
  }
  const std::string *text = std::get_if<std::string>(name);
  if (text == nullptr) {
    return PacketID::Unknown;
  }
  const auto id = packet_id_from_name(*text);
  return id.value_or(PacketID::Unknown);
}

auto derive_packet_id(const Payload &payload) -> PacketID {
  if (const auto *text = get_payload_if<TextPayload>(payload)) {
    return derive_packet_id(*text);
  }
  if (const auto *game = get_payload_if<GamePayload>(payload)) {
    return derive_packet_id(*game);
  }
  if (const auto *variant = get_payload_if<VariantPayload>(payload)) {
    return derive_packet_id(*variant);
  }
  return PacketID::Unknown;
}

} // namespace spx::packet
