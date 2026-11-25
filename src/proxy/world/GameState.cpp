#include "specialproxy/state/GameState.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "WorldParser.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/utils/ByteStream.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx {

namespace {

/** @brief Read one pipe field from a newline delimited packet body. */
auto find_field(std::string_view data, std::string_view key)
    -> std::optional<std::string_view> {
  size_t cursor = 0;
  while (cursor <= data.size()) {
    const size_t end = data.find('\n', cursor);
    const size_t stop = end == std::string_view::npos ? data.size() : end;
    const auto line = data.substr(cursor, stop - cursor);
    if (line.size() > key.size() && line.substr(0, key.size()) == key &&
        line[key.size()] == '|') {
      return line.substr(key.size() + 1);
    }
    if (end == std::string_view::npos) {
      break;
    }
    cursor = end + 1;
  }
  return std::nullopt;
}

/** @brief Read the nth pipe separated value of one field. */
auto nth_value(std::string_view field, size_t index)
    -> std::optional<std::string_view> {
  size_t cursor = 0;
  for (size_t current = 0;; ++current) {
    const size_t end = field.find('|', cursor);
    const size_t stop = end == std::string_view::npos ? field.size() : end;
    if (current == index) {
      return field.substr(cursor, stop - cursor);
    }
    if (end == std::string_view::npos) {
      return std::nullopt;
    }
    cursor = end + 1;
  }
}

/** @brief Parse one signed integer field value. */
auto int_field(std::string_view data, std::string_view key, size_t index)
    -> std::optional<int32_t> {
  const auto field = find_field(data, key);
  if (!field) {
    return std::nullopt;
  }
  const auto value = nth_value(*field, index);
  if (!value || value->empty()) {
    return std::nullopt;
  }
  int32_t result = 0;
  const char *begin = value->data();
  const char *end = begin + value->size();
  const auto parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return result;
}

/** @brief Parse one text field value. */
auto text_field(std::string_view data, std::string_view key) -> std::string {
  const auto field = find_field(data, key);
  if (!field) {
    return {};
  }
  const auto value = nth_value(*field, 0);
  return !value.has_value() || value->empty() ? std::string{}
                                              : std::string{*value};
}

/** @brief Apply one decoded packet to the shared managers. */
void observe_payload(GameState &state, const packet::Payload &payload) {
  if (const auto *variant =
          packet::get_payload_if<packet::VariantPayload>(payload)) {
    const auto *name = variant->list.get<std::string>(0);
    if (name == nullptr) {
      return;
    }
    if (*name == "OnSpawn") {
      const auto *body = variant->list.get<std::string>(1);
      if (body == nullptr) {
        return;
      }
      const auto net_id = int_field(*body, "netID", 0);
      if (!net_id) {
        return;
      }
      state::Player player;
      player.net_id = *net_id;
      player.user_id = int_field(*body, "userID", 0).value_or(0);
      player.name = text_field(*body, "name");
      player.country = text_field(*body, "country");
      const auto x = int_field(*body, "posXY", 0);
      const auto y = int_field(*body, "posXY", 1);
      player.x = static_cast<float>(x.value_or(0));
      player.y = static_cast<float>(y.value_or(0));
      player.facing_left = int_field(*body, "invis", 0).value_or(0) != 0;
      const bool is_local = text_field(*body, "type") == "local";
      state.players().upsert(std::move(player));
      if (is_local) {
        state.set_local_net_id(*net_id);
        spx::log::infof("state", "local player net_id=%d", *net_id);
      }
    } else if (*name == "OnRemove") {
      const auto *body = variant->list.get<std::string>(1);
      if (body == nullptr) {
        return;
      }
      const auto net_id = int_field(*body, "netID", 0);
      if (net_id) {
        state.players().remove(*net_id);
      }
    }
    return;
  }

  const auto *game = packet::get_payload_if<packet::GamePayload>(payload);
  if (game == nullptr) {
    return;
  }
  const auto type = static_cast<packet::PacketType>(game->packet.type);
  switch (type) {
  case packet::PacketType::SendMapData: {
    const auto parsed = state::parse_world_data(game->extra);
    state.tiles().set_info(parsed.info);
    state.objects().set_info(parsed.info);
    state.npcs().set_info(parsed.info);
    state.tiles().clear();
    state.objects().clear();
    state.objects().set_drop_id(parsed.drop_id);
    state.npcs().clear();
    for (auto &tile : parsed.tiles) {
      state.tiles().upsert(std::move(tile));
    }
    for (auto &object : parsed.objects) {
      state.objects().upsert(object);
    }
    spx::log::infof(
        "state", "world %s v%u %ux%u tiles=%zu objects=%zu complete=%d",
        parsed.info.name.c_str(), static_cast<unsigned>(parsed.info.version),
        parsed.info.width, parsed.info.height, state.tiles().size(),
        state.objects().size(), parsed.complete ? 1 : 0);
    break;
  }
  case packet::PacketType::SendTileUpdateData:
  case packet::PacketType::SendTileUpdateDataMultiple: {
    for (auto &tile : state::parse_tile_updates(game->extra, game->packet.int_x,
                                                game->packet.int_y)) {
      state.tiles().upsert(std::move(tile));
    }
    break;
  }
  case packet::PacketType::ItemChangeObject: {
    const auto change_type = static_cast<int32_t>(game->packet.net_id);
    const auto packet_item = static_cast<uint32_t>(game->packet.int_data);
    const auto item_net = static_cast<uint32_t>(game->packet.item_net_id);
    const auto amount = static_cast<uint32_t>(game->packet.float_var);
    spx::log::infof("state",
                    "item object change=%d packet_item=%u item_net=%u "
                    "amount=%u",
                    change_type, packet_item, item_net, amount);
    state::WorldObject object;
    object.item_id = static_cast<uint16_t>(packet_item);
    object.amount = amount > 255u ? 255u : static_cast<uint8_t>(amount);
    object.x = game->packet.pos_x;
    object.y = game->packet.pos_y;
    object.flags = static_cast<uint8_t>(game->packet.flags);
    if (change_type == -1) {
      object.object_id = state.objects().allocate_drop_id();
      state.objects().upsert(object);
      spx::log::infof("state", "object drop item=%u amount=%u id=%u",
                      packet_item, amount, object.object_id);
    } else if (change_type == -3) {
      object.object_id = item_net;
      state.objects().upsert(object);
    } else {
      const uint32_t object_id = packet_item != 0 ? packet_item : item_net;
      const auto stored = [&]() -> std::optional<state::WorldObject> {
        for (const auto &candidate : state.objects().snapshot()) {
          if (candidate.object_id == object_id) {
            return candidate;
          }
        }
        return std::nullopt;
      }();
      if (!stored) {
        spx::log::warningf("state", "collect object_id=%u not tracked",
                           object_id);
        break;
      }
      state.objects().remove(object_id);
      if (state.local_net_id() >= 0 && change_type == state.local_net_id()) {
        const uint32_t collected_item = stored->item_id;
        const uint32_t collected_amount = stored->amount;
        uint32_t current = 0;
        for (const auto &held : state.players().inventory()) {
          if (held.id == collected_item) {
            current += held.count;
          }
        }
        const uint32_t room = current < 200u ? 200u - current : 0u;
        const uint32_t added =
            collected_amount < room ? collected_amount : room;
        if (added > 0) {
          state.players().modify_inventory(static_cast<int32_t>(collected_item),
                                           static_cast<int32_t>(added));
          spx::log::infof("state", "collect item=%u amount=%u added=%u",
                          collected_item, collected_amount, added);
        }
      }
    }
    break;
  }
  case packet::PacketType::Npc: {
    state::Npc npc;
    npc.net_id = static_cast<int32_t>(game->packet.net_id);
    npc.npc_id = static_cast<uint32_t>(game->packet.item_net_id);
    npc.x = game->packet.pos_x;
    npc.y = game->packet.pos_y;
    npc.flags = game->packet.flags;
    state.npcs().upsert(npc);
    break;
  }
  case packet::PacketType::SendInventoryState: {
    utils::ByteStream<uint16_t> stream{game->extra};
    uint8_t version = 0;
    uint32_t slot_count = 0;
    uint16_t item_count = 0;
    if (!stream.read(version) || !stream.read(slot_count) ||
        !stream.read(item_count)) {
      break;
    }
    std::vector<state::InventoryItem> items;
    items.reserve(item_count);
    bool complete = true;
    for (uint16_t i = 0; i < item_count; ++i) {
      uint16_t raw_id = 0;
      state::InventoryItem item;
      if (!stream.read(raw_id) || !stream.read(item.count) ||
          !stream.read(item.flags)) {
        complete = false;
        break;
      }
      item.id = raw_id;
      if (item.id != 0) {
        items.push_back(item);
      }
    }
    if (complete) {
      const size_t loaded = items.size();
      state.players().set_inventory(version, slot_count, std::move(items));
      spx::log::infof("state", "inventory version=%u slots=%u items=%zu",
                      version, slot_count, loaded);
    }
    break;
  }
  case packet::PacketType::ModifyItemInventory: {
    const auto *raw = reinterpret_cast<const uint8_t *>(&game->packet);
    const int32_t removal = raw[2];
    const int32_t addition = raw[3];
    state.players().modify_inventory(game->packet.int_data, addition - removal);
    break;
  }
  case packet::PacketType::State: {
    const auto net_id = static_cast<int32_t>(game->packet.net_id);
    auto player = state.players().find(net_id);
    if (player) {
      player->x = game->packet.pos_x;
      player->y = game->packet.pos_y;
      player->facing_left =
          (game->packet.flags & packet::PacketFlagRotateLeft) != 0u;
      state.players().upsert(std::move(*player));
    }
    break;
  }
  default:
    break;
  }
}

} // namespace

auto GameState::instance() -> GameState & {
  static GameState state;
  return state;
}

auto GameState::players() -> state::PlayerManager & { return players_; }
auto GameState::tiles() -> state::TileManager & { return tiles_; }
auto GameState::objects() -> state::ObjectManager & { return objects_; }
auto GameState::npcs() -> state::NpcManager & { return npcs_; }

void GameState::observe(std::span<const std::byte> frame) {
  auto payload = packet::PacketDecoder::parse_payload(frame);
  if (!payload) {
    return;
  }
  observe_payload(*this, *payload);
}

auto GameState::local_net_id() const noexcept -> int32_t {
  return local_net_id_;
}

void GameState::set_local_net_id(int32_t net_id) noexcept {
  local_net_id_ = net_id;
}

void GameState::reset() {
  players_.clear();
  tiles_.clear();
  objects_.clear();
  npcs_.clear();
  local_net_id_ = -1;
}

auto player_manager() -> state::PlayerManager & {
  return GameState::instance().players();
}

auto tile_manager() -> state::TileManager & {
  return GameState::instance().tiles();
}

auto object_manager() -> state::ObjectManager & {
  return GameState::instance().objects();
}

auto npc_manager() -> state::NpcManager & {
  return GameState::instance().npcs();
}

} // namespace spx
