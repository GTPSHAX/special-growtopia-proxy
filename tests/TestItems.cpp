#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <zlib.h>
}

#include "../src/proxy/item/ItemStore.hpp"
#include "specialproxy/item/ItemData.hpp"
#include "specialproxy/item/ItemDatabase.hpp"
#include "specialproxy/state/GameState.hpp"
#include "specialproxy/utils/ByteStream.hpp"

namespace {

auto compress(const std::vector<std::byte> &input) -> std::vector<std::byte> {
  uLongf size = compressBound(static_cast<uLong>(input.size()));
  std::vector<std::byte> output(size);
  const int status = compress2(reinterpret_cast<Bytef *>(output.data()), &size,
                               reinterpret_cast<const Bytef *>(input.data()),
                               static_cast<uLong>(input.size()), Z_BEST_SPEED);
  assert(status == Z_OK);
  output.resize(size);
  return output;
}

auto make_items_dat() -> std::vector<std::byte> {
  std::vector<std::byte> data;
  data.push_back(std::byte{27});
  data.push_back(std::byte{0});
  data.push_back(std::byte{2});
  data.push_back(std::byte{0});
  data.push_back(std::byte{0});
  data.push_back(std::byte{0});
  for (int i = 0; i < 64; ++i) {
    data.push_back(static_cast<std::byte>(i));
  }
  return data;
}

auto test_hash_request() -> void {
  auto &store = spx::item::ItemStore::instance();
  assert(!store.note_upstream_hash(0));
  assert(store.note_upstream_hash(0x12345678u));
  assert(!store.note_upstream_hash(0x12345678u));
}

auto test_upstream_capture() -> void {
  const auto original = make_items_dat();
  const auto compressed = compress(original);

  assert(!spx::item_data_ready());
  assert(spx::item::ItemStore::instance().accept(compressed));

  const auto stored = spx::item_data();
  assert(spx::item_data_ready());
  assert(stored.valid);
  assert(stored.version == 27);
  assert(stored.count == 2);
  assert(stored.bytes == original);
  assert(stored.checksum != 0);

  const auto checksum = stored.checksum;
  assert(spx::item::ItemStore::instance().accept(compressed));
  assert(spx::item_data().checksum == checksum);
}

auto encrypt_name(std::string_view plain, uint32_t id) -> std::string {
  constexpr std::string_view key{"PBG892FXX982ABC*"};
  std::string out;
  out.reserve(plain.size());
  for (size_t i = 0; i < plain.size(); ++i) {
    out.push_back(static_cast<char>(plain[i] ^ key[(i + id) % key.size()]));
  }
  return out;
}

auto write_item_record(spx::utils::ByteStream<uint16_t> &stream, uint32_t id,
                       std::string_view name) -> void {
  stream.write(id);
  stream.write(static_cast<uint16_t>(0));
  stream.write(static_cast<uint8_t>(17));
  stream.write(static_cast<uint8_t>(0));
  stream.write(encrypt_name(name, id));
  stream.write(std::string{"texture"});
  stream.write(static_cast<uint32_t>(1));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint16_t>(5));
  stream.write(static_cast<uint8_t>(200));
  stream.write(std::string{});
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint16_t>(0));
  const std::array<std::byte, 62> padding{};
  stream.write_data(padding);
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(std::string{});
  stream.write(static_cast<uint32_t>(0));
  const std::array<std::byte, 9> body_parts{};
  stream.write_data(body_parts);
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint8_t>(0));
  for (int i = 0; i < 6; ++i) {
    stream.write(static_cast<uint32_t>(0));
  }
  stream.write(std::string{});
  stream.write(std::string{});
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<int32_t>(0));
  const std::array<std::byte, 9> renderer_padding{};
  stream.write_data(renderer_padding);
  stream.write(static_cast<uint16_t>(0));
  stream.write(std::string{});
  stream.write(static_cast<uint16_t>(0));
  stream.write(static_cast<uint16_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(std::string{});
  stream.write(static_cast<uint32_t>(0));
  stream.write(static_cast<uint8_t>(0));
  stream.write(std::string{});
}

auto test_item_database() -> void {
  spx::utils::ByteStream<uint16_t> stream;
  stream.write(static_cast<uint8_t>(27));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint8_t>(2));
  stream.write(static_cast<uint8_t>(0));
  stream.write(static_cast<uint16_t>(0));
  write_item_record(stream, 0, "Test Item");
  write_item_record(stream, 1, "Second Item");

  auto &database = spx::item_database();
  database.clear();
  assert(database.parse(stream.take_data()));
  assert(database.ready());
  assert(database.version() == 27);
  assert(database.size() == 2);
  const auto item = database.find(0);
  assert(item.has_value());
  assert(item->name == "Test Item");
  assert(item->id == 0);
  assert(item->rarity == 5);
  assert(item->max_amount == 200);
  assert(!database.find(9).has_value());
  assert(spx::item_name(0) == "Test Item");
  assert(spx::item_name(1) == "Second Item");
  assert(spx::item_name(9).empty());
  const auto resolved = spx::item_info(0);
  assert(resolved.has_value() && resolved->type == 17);

  spx::state::Tile tile;
  tile.x = 1;
  tile.y = 2;
  tile.foreground = 0;
  tile.background = 9;
  spx::tile_manager().upsert(tile);
  const auto foreground = spx::tile_manager().foreground_item(1, 2);
  assert(foreground.has_value() && foreground->name == "Test Item");
  assert(!spx::tile_manager().background_item(1, 2).has_value());
}

auto test_invalid_payloads() -> void {
  const std::vector<std::byte> garbage{std::byte{1}, std::byte{2},
                                       std::byte{3}};
  assert(!spx::item::ItemStore::instance().accept(garbage));

  const std::vector<std::byte> short_header{std::byte{1}, std::byte{2},
                                            std::byte{3}, std::byte{4}};
  assert(!spx::item::ItemStore::instance().accept(compress(short_header)));

  assert(spx::item_data_ready());
}

} // namespace

auto main() -> int {
  test_hash_request();
  test_upstream_capture();
  test_item_database();
  test_invalid_payloads();
  return 0;
}
