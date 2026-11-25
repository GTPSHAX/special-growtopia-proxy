#include "WorldParser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "specialproxy/utils/ByteStream.hpp"

namespace spx::state {

namespace {

using Stream = utils::ByteStream<uint16_t>;

constexpr uint16_t kTileFlagExtra = 1u << 0;
constexpr uint16_t kTileFlagLocked = 1u << 1;

auto skip_bytes(Stream &stream, size_t count) -> bool {
  std::vector<std::byte> scratch(count);
  return stream.read_data(scratch.data(), count);
}

auto parse_extra(Stream &stream, uint8_t type, uint16_t foreground,
                 TileExtra &out) -> bool {
  out.type = type;
  switch (type) {
  case 0: {
    break;
  }
  case 1: {
    uint8_t flags = 0;
    if (!stream.read(out.text) || !stream.read(flags))
      return false;
    out.value1 = flags;
    break;
  }
  case 2: {
    uint32_t unknown = 0;
    if (!stream.read(out.text) || !stream.read(unknown))
      return false;
    out.value1 = unknown;
    break;
  }
  case 3: {
    uint8_t settings = 0;
    uint32_t owner = 0;
    uint32_t access_count = 0;
    if (!stream.read(settings) || !stream.read(owner) ||
        !stream.read(access_count))
      return false;
    out.value1 = settings;
    out.value2 = owner;
    for (uint32_t i = 0; i < access_count; ++i) {
      int32_t access = 0;
      if (!stream.read(access))
        return false;
      out.values.push_back(static_cast<uint32_t>(access));
    }
    uint32_t minimum_level = 0;
    uint32_t world_timer = 0;
    if (!stream.read(minimum_level) || !stream.read(world_timer))
      return false;
    if (foreground == 5814 && !skip_bytes(stream, 16))
      return false;
    break;
  }
  case 4: {
    uint8_t item = 0;
    if (!stream.read(out.value1) || !stream.read(item))
      return false;
    out.value2 = item;
    break;
  }
  case 6:
  case 7:
  case 12: {
    std::string second_text;
    std::string third_text;
    uint8_t unknown = 0;
    if (!stream.read(out.text) || !stream.read(second_text) ||
        !stream.read(third_text) || !stream.read(unknown))
      return false;
    out.value1 = unknown;
    break;
  }
  case 8: {
    uint8_t symbol = 0;
    if (!stream.read(symbol))
      return false;
    out.value1 = symbol;
    break;
  }
  case 9: {
    if (!stream.read(out.value1))
      return false;
    if (foreground == 10656 && !skip_bytes(stream, 4))
      return false;
    break;
  }
  case 10: {
    uint8_t tile_type = 0;
    if (!stream.read(out.value1) || !stream.read(tile_type))
      return false;
    out.value2 = tile_type;
    break;
  }
  case 11: {
    uint32_t player_id = 0;
    if (!stream.read(player_id) || !stream.read(out.text))
      return false;
    out.value1 = player_id;
    break;
  }
  case 14: {
    std::array<uint16_t, 10> clothes{};
    uint8_t unknown = 0;
    uint16_t unknown2 = 0;
    uint16_t unknown3 = 0;
    if (!stream.read(out.text) || !stream.read(unknown) ||
        !stream.read(unknown2) || !stream.read(unknown3))
      return false;
    for (auto &value : clothes) {
      if (!stream.read(value))
        return false;
    }
    out.values.assign(clothes.begin(), clothes.end());
    out.value1 = unknown;
    break;
  }
  case 15:
  case 16:
  case 18:
  case 27:
  case 38:
  case 39:
  case 40:
  case 41:
  case 50:
  case 57:
  case 66:
  case 73: {
    if (!stream.read(out.value1))
      return false;
    break;
  }
  case 17:
  case 22:
  case 34:
  case 45:
  case 51:
  case 52:
  case 58:
  case 59:
  case 74: {
    break;
  }
  case 42: {
    if (!skip_bytes(stream, 21))
      return false;
    break;
  }
  case 19: {
    std::array<uint16_t, 8> clothes{};
    for (auto &value : clothes) {
      if (!stream.read(value))
        return false;
    }
    out.values.assign(clothes.begin(), clothes.end());
    break;
  }
  case 20: {
    uint16_t count = 0;
    if (!stream.read(count))
      return false;
    for (uint16_t i = 0; i < count; ++i) {
      uint8_t value = 0;
      if (!stream.read(value))
        return false;
      out.values.push_back(value);
    }
    break;
  }
  case 21: {
    uint32_t unknown = 0;
    uint8_t small = 0;
    if (!stream.read(out.text) || !stream.read(unknown) || !stream.read(small))
      return false;
    out.value1 = unknown;
    out.value2 = small;
    break;
  }
  case 23: {
    if (!stream.read(out.value1))
      return false;
    break;
  }
  case 53: {
    if (!stream.read(out.value1) || !stream.read(out.value2))
      return false;
    break;
  }
  case 24: {
    int32_t price = 0;
    if (!stream.read(out.value1) || !stream.read(price))
      return false;
    out.value2 = static_cast<uint32_t>(price);
    break;
  }
  case 25: {
    uint32_t count = 0;
    uint8_t flags = 0;
    if (!stream.read(flags) || !stream.read(count))
      return false;
    out.value1 = flags;
    for (uint32_t i = 0; i < count / 2; ++i) {
      uint32_t item = 0;
      uint32_t pounds = 0;
      if (!stream.read(item) || !stream.read(pounds))
        return false;
      out.pairs.emplace_back(item, pounds);
    }
    break;
  }
  case 26: {
    std::array<uint8_t, 5> data{};
    for (auto &value : data) {
      if (!stream.read(value))
        return false;
    }
    out.values.assign(data.begin(), data.end());
    break;
  }
  case 28: {
    uint8_t harvested = 0;
    uint16_t age = 0;
    uint16_t unknown = 0;
    uint8_t decoration = 0;
    if (!stream.read(harvested) || !stream.read(age) || !stream.read(unknown) ||
        !stream.read(decoration))
      return false;
    out.value1 = harvested;
    out.value2 = age;
    break;
  }
  case 30: {
    uint8_t instrument = 0;
    if (!stream.read(instrument) || !stream.read(out.value1))
      return false;
    out.value2 = instrument;
    break;
  }
  case 31: {
    uint8_t flags = 0;
    uint8_t can_feed = 0;
    std::array<uint32_t, 10> values{};
    if (!stream.read(flags) || !stream.read(out.text))
      return false;
    for (size_t i = 0; i < 2; ++i) {
      if (!stream.read(values[i]))
        return false;
    }
    if (!stream.read(can_feed))
      return false;
    for (size_t i = 2; i < values.size(); ++i) {
      if (!stream.read(values[i]))
        return false;
    }
    out.value1 = flags;
    out.values.assign(values.begin(), values.end());
    break;
  }
  case 32:
  case 67: {
    uint32_t count = 0;
    if (!stream.read(count))
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t value = 0;
      if (!stream.read(value))
        return false;
      out.values.push_back(value);
    }
    break;
  }
  case 77: {
    uint32_t count = 0;
    if (!stream.read(count))
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t value = 0;
      if (!stream.read(value))
        return false;
      out.values.push_back(value);
    }
    if (!stream.read(out.value1))
      return false;
    break;
  }
  case 33: {
    if (foreground == 3394 && !stream.read(out.text))
      return false;
    break;
  }
  case 35: {
    uint32_t item = 0;
    if (!stream.read(item) || !stream.read(out.text))
      return false;
    out.value1 = item;
    break;
  }
  case 47: {
    uint32_t item = 0;
    uint8_t extra = 0;
    if (!stream.read(item) || !stream.read(out.text) || !stream.read(extra))
      return false;
    out.value1 = item;
    break;
  }
  case 36: {
    uint32_t base = 0;
    uint32_t pet1 = 0;
    uint32_t pet2 = 0;
    if (!stream.read(out.text) || !stream.read(base) || !stream.read(pet1) ||
        !stream.read(pet2))
      return false;
    out.value1 = base;
    out.value2 = pet1;
    break;
  }
  case 37: {
    uint32_t total = 0;
    uint32_t unknown = 0;
    if (!stream.read(out.text) || !stream.read(total) || !stream.read(unknown))
      return false;
    out.value1 = total;
    for (uint32_t i = 0; i < total; ++i) {
      uint32_t pet = 0;
      if (!stream.read(pet))
        return false;
      out.values.push_back(pet);
    }
    break;
  }
  case 43: {
    std::array<uint32_t, 4> corners{};
    for (auto &value : corners) {
      if (!stream.read(value))
        return false;
    }
    out.values.assign(corners.begin(), corners.end());
    break;
  }
  case 44:
  case 84: {
    uint8_t unknown = 0;
    uint32_t owner = 0;
    uint32_t count = 0;
    if (!stream.read(unknown) || !stream.read(owner) || !stream.read(count))
      return false;
    out.value1 = unknown;
    out.value2 = owner;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t uid = 0;
      if (!stream.read(uid))
        return false;
      out.values.push_back(uid);
    }
    break;
  }
  case 48: {
    std::array<uint8_t, 5> unknown3{};
    uint8_t unknown4 = 0;
    uint16_t unknown5 = 0;
    uint16_t face = 0;
    uint16_t hat = 0;
    uint16_t hair = 0;
    uint32_t unknown6 = 0;
    if (!stream.read(out.text) || !stream.read(out.value1) ||
        !stream.read(out.value2))
      return false;
    for (auto &value : unknown3) {
      if (!stream.read(value))
        return false;
    }
    if (!stream.read(unknown4) || !stream.read(unknown5) ||
        !stream.read(face) || !stream.read(hat) || !stream.read(hair) ||
        !stream.read(unknown6))
      return false;
    if (hat == 12958 && !stream.read(out.text))
      return false;
    break;
  }
  case 49: {
    uint8_t flags = 0;
    if (!stream.read(out.value1) || !stream.read(out.value2) ||
        !stream.read(flags))
      return false;
    break;
  }
  case 54: {
    uint16_t length = 0;
    if (!stream.read(length))
      return false;
    for (uint16_t i = 0; i < length / 13; ++i) {
      if (!skip_bytes(stream, 3))
        return false;
      uint32_t item = 0;
      uint32_t amount = 0;
      if (!stream.read(item) || !skip_bytes(stream, 2) || !stream.read(amount))
        return false;
      out.pairs.emplace_back(item, amount);
    }
    break;
  }
  case 55: {
    uint32_t temperature = 0;
    uint32_t count = 0;
    if (!stream.read(temperature) || !stream.read(count))
      return false;
    out.value1 = temperature;
    for (uint32_t i = 0; i < count / 2; ++i) {
      uint32_t item = 0;
      uint32_t time = 0;
      if (!stream.read(item) || !stream.read(time))
        return false;
      out.pairs.emplace_back(item, time);
    }
    for (int i = 0; i < 3; ++i) {
      uint32_t value = 0;
      if (!stream.read(value))
        return false;
    }
    break;
  }
  case 56: {
    uint32_t volume = 0;
    if (!stream.read(out.text) || !stream.read(volume))
      return false;
    out.value1 = volume;
    break;
  }
  case 60: {
    uint8_t team = 0;
    if (!stream.read(out.value1) || !stream.read(team))
      return false;
    out.value2 = team;
    break;
  }
  case 61: {
    std::array<uint8_t, 8> unknown1{};
    std::array<uint8_t, 5> unknown3{};
    uint16_t status = 0;
    uint32_t unknown2 = 0;
    uint32_t fish_level = 0;
    if (!stream.read(out.value1) || !stream.read(status) ||
        !stream.read(out.value2) || !stream.read(unknown2))
      return false;
    for (auto &value : unknown1) {
      if (!stream.read(value))
        return false;
    }
    if (!stream.read(fish_level) || !stream.read(unknown2))
      return false;
    for (auto &value : unknown3) {
      if (!stream.read(value))
        return false;
    }
    break;
  }
  case 62: {
    uint32_t amount = 0;
    uint16_t flags = 0;
    uint32_t limit = 0;
    if (!stream.read(out.value1) || !stream.read(amount) ||
        !stream.read(flags) || !stream.read(limit))
      return false;
    out.value2 = amount;
    break;
  }
  case 63: {
    uint32_t count = 0;
    uint32_t sync_timer = 0;
    uint32_t activated = 0;
    if (!stream.read(count))
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t command = 0;
      uint32_t used = 0;
      if (!stream.read(command) || !stream.read(used) || !skip_bytes(stream, 7))
        return false;
      out.pairs.emplace_back(command, used);
    }
    if (!stream.read(sync_timer) || !stream.read(activated))
      return false;
    out.value1 = activated;
    break;
  }
  case 65: {
    if (!skip_bytes(stream, 17))
      return false;
    break;
  }
  case 68: {
    uint32_t count = 0;
    if (!stream.read(out.value1) || !stream.read(out.text) ||
        !stream.read(out.text) || !stream.read(count))
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t item = 0;
      if (!stream.read(item))
        return false;
      out.values.push_back(item);
    }
    break;
  }
  case 69: {
    uint32_t next = 0;
    uint32_t item = 0;
    uint32_t enabled = 0;
    if (!stream.read(out.value1) || !stream.read(next) || !stream.read(item) ||
        !stream.read(enabled))
      return false;
    out.value2 = item;
    break;
  }
  case 72: {
    uint32_t third = 0;
    if (!stream.read(out.value1) || !stream.read(out.value2) ||
        !stream.read(third))
      return false;
    break;
  }
  case 75: {
    uint16_t unknown = 0;
    uint8_t ascii = 0;
    if (!stream.read(out.value1) || !stream.read(unknown))
      return false;
    if (out.value1 == 2 && !stream.read(ascii))
      return false;
    break;
  }
  case 79: {
    if (!stream.read(out.value1))
      return false;
    break;
  }
  case 80: {
    uint32_t unknown = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!stream.read(out.value1) || !stream.read(unknown) ||
        !stream.read(red) || !stream.read(green) || !stream.read(blue)) {
      return false;
    }
    break;
  }
  case 81:
  case 142: {
    uint32_t owner = 0;
    uint16_t count = 0;
    if (!stream.read(owner) || !skip_bytes(stream, 2) || !stream.read(count))
      return false;
    out.value1 = owner;
    for (uint16_t i = 0; i < count; ++i) {
      uint32_t uid = 0;
      if (!stream.read(uid))
        return false;
      out.values.push_back(uid);
    }
    break;
  }
  default: {
    return false;
  }
  }
  out.parsed = true;
  return true;
}

auto parse_tile(Stream &stream, Tile &tile) -> bool {
  uint16_t parent = 0;
  uint16_t flags = 0;
  if (!stream.read(tile.foreground) || !stream.read(tile.background) ||
      !stream.read(parent) || !stream.read(flags)) {
    return false;
  }
  tile.flags = flags;
  if ((flags & kTileFlagLocked) != 0u) {
    uint16_t lock = 0;
    if (!stream.read(lock)) {
      return false;
    }
  }
  if ((flags & kTileFlagExtra) != 0u) {
    uint8_t type = 0;
    if (!stream.read(type) ||
        !parse_extra(stream, type, tile.foreground, tile.extra)) {
      return false;
    }
  }
  return true;
}

auto consumed_all(const Stream &stream) noexcept -> bool {
  return stream.read_offset() == stream.size();
}

} // namespace

auto parse_world_data(std::span<const std::byte> extra) -> WorldParseResult {
  WorldParseResult result;
  if (extra.empty()) {
    return result;
  }

  Stream stream{extra};
  if (!stream.read(result.info.version) || !skip_bytes(stream, 4) ||
      !stream.read(result.info.name) || !stream.read(result.info.width) ||
      !stream.read(result.info.height) ||
      !stream.read(result.info.tile_count) || !skip_bytes(stream, 5)) {
    return result;
  }

  const int32_t width = std::max<int32_t>(result.info.width, 1);
  const uint32_t tile_count =
      std::min<uint32_t>(result.info.tile_count, 100000u);
  result.tiles.reserve(tile_count);
  for (uint32_t i = 0; i < tile_count; ++i) {
    Tile tile;
    if (!parse_tile(stream, tile)) {
      return result;
    }
    tile.x = static_cast<int32_t>(i % static_cast<uint32_t>(width));
    tile.y = static_cast<int32_t>(i / static_cast<uint32_t>(width));
    result.tiles.push_back(std::move(tile));
  }

  if (!skip_bytes(stream, 12)) {
    return result;
  }
  uint32_t object_count = 0;
  if (!stream.read(object_count) || !stream.read(result.drop_id)) {
    return result;
  }
  object_count = std::min<uint32_t>(object_count, 100000u);
  result.objects.reserve(object_count);
  for (uint32_t i = 0; i < object_count; ++i) {
    WorldObject object;
    if (!stream.read(object.item_id) || !stream.read(object.x) ||
        !stream.read(object.y) || !stream.read(object.amount) ||
        !stream.read(object.flags) || !stream.read(object.object_id)) {
      return result;
    }
    result.objects.push_back(object);
  }

  result.complete = true;
  return result;
}

auto parse_tile_updates(std::span<const std::byte> extra, int32_t header_x,
                        int32_t header_y) -> std::vector<Tile> {
  std::vector<Tile> result;
  if (extra.size() < 8) {
    return result;
  }

  Stream single{extra};
  Tile first;
  if (parse_tile(single, first)) {
    first.x = header_x;
    first.y = header_y;
    result.push_back(std::move(first));
    if (consumed_all(single) || single.size() - single.read_offset() < 4) {
      return result;
    }
  }

  result.clear();
  Stream batch{extra};
  while (batch.read_offset() + 4 <= batch.size()) {
    int32_t x = 0;
    int32_t y = 0;
    if (!batch.read(x) || !batch.read(y)) {
      return {};
    }
    if (x == -1 && y == -1) {
      break;
    }
    Tile tile;
    if (!parse_tile(batch, tile)) {
      return {};
    }
    tile.x = x;
    tile.y = y;
    result.push_back(std::move(tile));
  }
  return consumed_all(batch) ? result : std::vector<Tile>{};
}

} // namespace spx::state
