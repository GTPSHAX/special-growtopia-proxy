#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/state/GameState.hpp"
#include "specialproxy/utils/ByteStream.hpp"

using namespace spx;

namespace {

auto variant_frame(const packet::VariantList &list) -> std::vector<std::byte> {
  return packet::serialize_payload(
      packet::VariantPayload{packet::GameUpdatePacket{}, list});
}

auto game_frame(packet::PacketType type, packet::GameUpdatePacket header,
                std::vector<std::byte> extra) -> std::vector<std::byte> {
  header.type = static_cast<uint8_t>(type);
  return packet::serialize_payload(
      packet::GamePayload{header, std::move(extra)});
}

auto test_manager_hierarchy() -> void {
  state::PlayerManager players;
  assert(players.empty());
  state::Player player;
  player.net_id = 5;
  player.user_id = 42;
  player.name = "Tester";
  players.upsert(player);
  assert(players.size() == 1);
  const auto found = players.find(5);
  assert(found.has_value() && found->name == "Tester");
  assert(players.first().has_value());
  players.remove(5);
  assert(players.empty());

  state::WorldInfo info;
  info.name = "WORLD";
  info.width = 100;
  info.height = 200;
  state::TileManager tiles;
  tiles.set_info(info);
  assert(tiles.info().name == "WORLD");
  state::Tile tile;
  tile.x = 3;
  tile.y = 4;
  tile.foreground = 10;
  tiles.upsert(tile);
  const auto stored = tiles.get(3, 4);
  assert(stored.has_value() && stored->foreground == 10);
  assert(tiles.snapshot().size() == 1);
  tiles.clear();
  assert(tiles.empty());

  state::ObjectManager objects;
  state::WorldObject object;
  object.object_id = 7;
  object.item_id = 242;
  objects.upsert(object);
  assert(objects.snapshot().size() == 1);
  objects.remove(7);
  assert(objects.empty());

  state::NpcManager npcs;
  state::Npc npc;
  npc.net_id = 9;
  npc.npc_id = 12;
  npcs.upsert(npc);
  assert(npcs.snapshot().size() == 1);
  npcs.remove(9);
  assert(npcs.empty());
}

auto test_observe_players() -> void {
  auto &game = GameState::instance();
  game.reset();

  packet::VariantList spawn;
  spawn.set(0, packet::VariantValue{std::string{"OnSpawn"}});
  spawn.set(1, packet::VariantValue{
                   std::string{"spawn|main\nnetID|5\nuserID|42\nname|Tester\n"
                               "country|us\nposXY|100|200\n"}});
  game.observe(variant_frame(spawn));

  const auto player = game.players().find(5);
  assert(player.has_value());
  assert(player->name == "Tester");
  assert(player->user_id == 42);
  assert(player->x == 100.0F && player->y == 200.0F);

  packet::GameUpdatePacket move{};
  move.net_id = 5;
  move.pos_x = 300.0F;
  move.pos_y = 400.0F;
  game.observe(game_frame(packet::PacketType::State, move, {}));
  const auto moved = game.players().find(5);
  assert(moved.has_value() && moved->x == 300.0F && moved->y == 400.0F);

  packet::VariantList remove;
  remove.set(0, packet::VariantValue{std::string{"OnRemove"}});
  remove.set(1, packet::VariantValue{std::string{"netID|5"}});
  game.observe(variant_frame(remove));
  assert(!game.players().find(5).has_value());
}

auto test_observe_collect() -> void {
  auto &game = GameState::instance();
  game.reset();

  packet::VariantList spawn;
  spawn.set(0, packet::VariantValue{std::string{"OnSpawn"}});
  spawn.set(1, packet::VariantValue{
                   std::string{"spawn|main\nnetID|1\nuserID|7\nname|Tester\n"
                               "type|local\ninvis|0\n"}});
  game.observe(variant_frame(spawn));
  assert(game.local_net_id() == 1);

  game.players().set_inventory(1, 4, {{242, 5, 0}});

  packet::GameUpdatePacket drop{};
  drop.net_id = static_cast<uint32_t>(-1);
  drop.item_net_id = 1;
  drop.int_data = 242;
  drop.float_var = 3.0F;
  drop.pos_x = 64.0F;
  drop.pos_y = 96.0F;
  game.observe(game_frame(packet::PacketType::ItemChangeObject, drop, {}));
  assert(game.objects().size() == 1);
  const auto objects = game.objects().snapshot();
  assert(objects[0].item_id == 242 && objects[0].amount == 3);
  assert(objects[0].x == 64.0F && objects[0].y == 96.0F);

  packet::GameUpdatePacket collect{};
  collect.net_id = 1;
  collect.int_data = static_cast<int32_t>(objects[0].object_id);
  game.observe(game_frame(packet::PacketType::ItemChangeObject, collect, {}));
  assert(game.objects().empty());
  const auto held = game.players().inventory();
  assert(held.size() == 1 && held[0].id == 242 && held[0].count == 8);

  game.observe(game_frame(packet::PacketType::ItemChangeObject, drop, {}));
  const auto remaining = game.objects().snapshot();
  assert(remaining.size() == 1);
  packet::GameUpdatePacket remote_collect{};
  remote_collect.net_id = 2;
  remote_collect.int_data = static_cast<int32_t>(remaining[0].object_id);
  game.observe(
      game_frame(packet::PacketType::ItemChangeObject, remote_collect, {}));
  assert(game.objects().empty());
  assert(game.players().inventory()[0].count == 8);
  game.reset();
  assert(game.local_net_id() == -1);
}

auto test_observe_world() -> void {
  auto &game = GameState::instance();
  game.reset();

  utils::ByteStream<std::uint16_t> stream;
  stream.write(static_cast<uint16_t>(27));
  const std::array<std::byte, 4> skipped{};
  stream.write_data(skipped);
  const std::string name{"WORLD"};
  stream.write(name);
  stream.write(static_cast<int32_t>(100));
  stream.write(static_cast<int32_t>(200));
  stream.write(static_cast<uint32_t>(2));
  const std::array<std::byte, 5> trailer{};
  stream.write_data(trailer);
  for (const uint16_t value : {10, 11, 0, 0, 20, 21, 0, 0}) {
    stream.write(value);
  }
  const std::array<std::byte, 12> object_header{};
  stream.write_data(object_header);
  stream.write(static_cast<uint32_t>(1));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint16_t>(242));
  stream.write(1.5F);
  stream.write(2.5F);
  stream.write(static_cast<uint8_t>(3));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint32_t>(7));
  game.observe(
      game_frame(packet::PacketType::SendMapData, {}, stream.take_data()));
  const auto info = game.tiles().info();
  assert(info.name == "WORLD");
  assert(info.width == 100 && info.height == 200);
  assert(info.version == 27);
  assert(game.tiles().size() == 2);
  assert(game.objects().size() == 1);
  const auto first_tile = game.tiles().get(0, 0);
  assert(first_tile.has_value() && first_tile->foreground == 10);
  const auto objects = game.objects().snapshot();
  assert(objects.size() == 1 && objects[0].item_id == 242);
  assert(objects[0].object_id == 7);

  std::vector<std::byte> extra;
  const uint16_t values[]{10, 11, 0, 12};
  for (const auto value : values) {
    const auto *bytes = reinterpret_cast<const std::byte *>(&value);
    extra.push_back(bytes[0]);
    extra.push_back(bytes[1]);
  }
  packet::GameUpdatePacket header{};
  header.int_x = 3;
  header.int_y = 4;
  game.observe(
      game_frame(packet::PacketType::SendTileUpdateData, header, extra));
  const auto tile = game.tiles().get(3, 4);
  assert(tile.has_value());
  assert(tile->foreground == 10 && tile->background == 11);
  assert(tile->flags == 12);
}

auto test_tile_extra() -> void {
  auto &game = GameState::instance();
  game.reset();

  utils::ByteStream<uint16_t> stream;
  stream.write(static_cast<uint16_t>(10));
  stream.write(static_cast<uint16_t>(11));
  stream.write(static_cast<uint16_t>(0));
  stream.write(static_cast<uint16_t>(1));
  stream.write(static_cast<uint8_t>(1));
  const std::string label{"door"};
  stream.write(label);
  stream.write(static_cast<uint8_t>(2));

  packet::GameUpdatePacket header{};
  header.int_x = 7;
  header.int_y = 8;
  game.observe(game_frame(packet::PacketType::SendTileUpdateData, header,
                          stream.take_data()));

  const auto tile = game.tiles().get(7, 8);
  assert(tile.has_value());
  assert(tile->extra.type == 1);
  assert(tile->extra.text == "door");
  assert(tile->extra.parsed);
}

auto test_inventory() -> void {
  auto &game = GameState::instance();
  game.reset();

  utils::ByteStream<uint16_t> stream;
  stream.write(static_cast<uint8_t>(7));
  stream.write(static_cast<uint32_t>(20));
  stream.write(static_cast<uint16_t>(2));
  stream.write(static_cast<uint16_t>(242));
  stream.write(static_cast<uint8_t>(5));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint16_t>(264));
  stream.write(static_cast<uint8_t>(3));
  stream.write(static_cast<uint8_t>(1));
  game.observe(game_frame(packet::PacketType::SendInventoryState, {},
                          stream.take_data()));

  const auto items = game.players().inventory();
  assert(items.size() == 2);
  assert(items[0].id == 242 && items[0].count == 5);
  assert(game.players().inventory_slot_count() == 20);

  packet::GameUpdatePacket change{};
  auto *raw = reinterpret_cast<uint8_t *>(&change);
  raw[2] = 2;
  raw[3] = 0;
  change.int_data = 242;
  game.observe(game_frame(packet::PacketType::ModifyItemInventory, change, {}));
  const auto updated = game.players().inventory();
  assert(updated.size() == 2 && updated[0].count == 3);
}

} // namespace

auto main() -> int {
  test_manager_hierarchy();
  test_observe_players();
  test_observe_collect();
  test_observe_world();
  test_tile_extra();
  test_inventory();
  return 0;
}
