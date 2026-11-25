#include "specialproxy/item/ItemDatabase.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "specialproxy/utils/ByteStream.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx {

namespace {

using Stream = utils::ByteStream<uint16_t>;

constexpr std::string_view kDecryptKey{"PBG892FXX982ABC*"};
constexpr uint8_t kMaxSupportedVersion{27};

/** @brief Decrypt one item name with the game name cipher. */
auto decrypt_name(std::string_view encrypted, uint32_t item_id) -> std::string {
  std::string out;
  out.reserve(encrypted.size());
  for (size_t i = 0; i < encrypted.size(); ++i) {
    const size_t key_index = (i + item_id) % kDecryptKey.size();
    out.push_back(static_cast<char>(encrypted[i] ^ kDecryptKey[key_index]));
  }
  return out;
}

/** @brief Advance a stream by a fixed number of bytes. */
auto skip_bytes(Stream &stream, size_t count) -> bool {
  std::vector<std::byte> scratch(count);
  return stream.read_data(scratch.data(), count);
}

/** @brief Read one item record and consume every version dependent field. */
auto parse_item(Stream &stream, uint8_t version) -> std::optional<ItemInfo> {
  ItemInfo item;
  if (!stream.read(item.id) || !stream.read(item.flags) ||
      !stream.read(item.type) || !stream.read(item.material)) {
    return std::nullopt;
  }

  if (!stream.read(item.name)) {
    return std::nullopt;
  }
  item.name = decrypt_name(item.name, item.id);

  if (!stream.read(item.texture_path) || !stream.read(item.texture_hash) ||
      !stream.read(item.visual_effect) || !stream.read(item.cooking_time) ||
      !stream.read(item.texture_x) || !stream.read(item.texture_y) ||
      !stream.read(item.texture_type) || !stream.read(item.stripey_wallpaper) ||
      !stream.read(item.collision_type) || !stream.read(item.health) ||
      !stream.read(item.reset_time) || !stream.read(item.clothing_type) ||
      !stream.read(item.rarity) || !stream.read(item.max_amount) ||
      !stream.read(item.extra_file) || !stream.read(item.extra_file_hash) ||
      !stream.read(item.animation_time) || !stream.read(item.pet_name) ||
      !stream.read(item.pet_prefix) || !stream.read(item.pet_suffix) ||
      !stream.read(item.pet_ability) || !stream.read(item.seed_base) ||
      !stream.read(item.seed_overlay) || !stream.read(item.tree_base) ||
      !stream.read(item.tree_leaves) || !stream.read(item.seed_color) ||
      !stream.read(item.seed_overlay_color) || !stream.read(item.ingredient) ||
      !stream.read(item.grow_time)) {
    return std::nullopt;
  }

  uint32_t fx_flags = 0;
  if (!stream.read(fx_flags)) {
    return std::nullopt;
  }
  std::string animating_coordinates;
  std::string animating_texture_file;
  std::string animating_coordinates_2;
  if (!stream.read(animating_coordinates) ||
      !stream.read(animating_texture_file) ||
      !stream.read(animating_coordinates_2)) {
    return std::nullopt;
  }

  uint32_t unknown1 = 0;
  uint32_t unknown2 = 0;
  uint16_t flags2 = 0;
  if (!stream.read(unknown1) || !stream.read(unknown2) ||
      !stream.read(flags2) || !skip_bytes(stream, 62)) {
    return std::nullopt;
  }
  if (!stream.read(item.tile_range) || !stream.read(item.vault_capacity) ||
      !stream.read(item.punch_options)) {
    return std::nullopt;
  }
  uint32_t unknown4 = 0;
  std::array<uint8_t, 9> body_parts{};
  if (!stream.read(unknown4) || !skip_bytes(stream, body_parts.size())) {
    return std::nullopt;
  }
  if (!stream.read(item.light_range)) {
    return std::nullopt;
  }
  uint32_t unknown5 = 0;
  if (!stream.read(unknown5)) {
    return std::nullopt;
  }
  uint8_t can_sit = 0;
  uint32_t player_offset_x = 0;
  uint32_t player_offset_y = 0;
  uint32_t chair_texture_x = 0;
  uint32_t chair_texture_y = 0;
  uint32_t chair_leg_x = 0;
  uint32_t chair_leg_y = 0;
  if (!stream.read(can_sit) || !stream.read(player_offset_x) ||
      !stream.read(player_offset_y) || !stream.read(chair_texture_x) ||
      !stream.read(chair_texture_y) || !stream.read(chair_leg_x) ||
      !stream.read(chair_leg_y)) {
    return std::nullopt;
  }
  std::string chair_texture_file;
  std::string renderer_data_file;
  if (!stream.read(chair_texture_file) || !stream.read(renderer_data_file)) {
    return std::nullopt;
  }
  uint32_t unknown6 = 0;
  int32_t renderer_hash = 0;
  if (!stream.read(unknown6) || !stream.read(renderer_hash) ||
      !skip_bytes(stream, 9)) {
    return std::nullopt;
  }
  uint16_t unknown8 = 0;
  if (!stream.read(unknown8) || !stream.read(item.info)) {
    return std::nullopt;
  }
  uint16_t ingredient1 = 0;
  uint16_t ingredient2 = 0;
  if (!stream.read(ingredient1) || !stream.read(ingredient2)) {
    return std::nullopt;
  }

  if (version >= 24) {
    uint8_t unknown9 = 0;
    if (!stream.read(unknown9))
      return std::nullopt;
  }
  if (version >= 25) {
    std::string hit_sound_fx;
    uint32_t hit_sound_hash = 0;
    if (!stream.read(hit_sound_fx) || !stream.read(hit_sound_hash)) {
      return std::nullopt;
    }
  }
  if (version >= 26) {
    uint8_t unknown10 = 0;
    if (!stream.read(unknown10))
      return std::nullopt;
  }
  if (version >= 27) {
    std::string trailing;
    if (!stream.read(trailing)) {
      return std::nullopt;
    }
  }
  return item;
}

} // namespace

auto ItemDatabase::instance() -> ItemDatabase & {
  static ItemDatabase database;
  return database;
}

auto ItemDatabase::parse(std::span<const std::byte> data) -> bool {
  if (data.empty()) {
    return false;
  }
  Stream stream{data};
  uint8_t version = 0;
  uint8_t unknown1 = 0;
  uint8_t count_low = 0;
  uint8_t count_high = 0;
  uint16_t unknown2 = 0;
  if (!stream.read(version) || !stream.read(unknown1) ||
      !stream.read(count_low) || !stream.read(count_high) ||
      !stream.read(unknown2)) {
    return false;
  }
  if (version > kMaxSupportedVersion || version == 0) {
    spx::log::warningf("item", "unsupported items.dat version %u", version);
    return false;
  }

  const uint32_t count = static_cast<uint32_t>(count_low) |
                         (static_cast<uint32_t>(count_high) << 8);
  std::vector<ItemInfo> parsed;
  parsed.reserve(std::min<uint32_t>(count, 100000u));
  for (uint32_t i = 0; i < count; ++i) {
    auto item = parse_item(stream, version);
    if (!item) {
      spx::log::warningf("item", "items.dat record %u truncated", i);
      return false;
    }
    if (item->id != i) {
      spx::log::warningf("item", "items.dat id mismatch at %u", i);
      return false;
    }
    parsed.push_back(std::move(*item));
  }

  const size_t parsed_count = parsed.size();
  {
    std::lock_guard<std::mutex> lock{mutex_};
    items_ = std::move(parsed);
    version_ = version;
    valid_ = true;
  }
  spx::log::infof("item", "items.dat parsed version=%u count=%zu", version,
                  parsed_count);
  return true;
}

auto ItemDatabase::find(uint32_t id) const -> std::optional<ItemInfo> {
  std::lock_guard<std::mutex> lock{mutex_};
  if (id >= items_.size()) {
    return std::nullopt;
  }
  return items_[id];
}

auto ItemDatabase::snapshot() const -> std::vector<ItemInfo> {
  std::lock_guard<std::mutex> lock{mutex_};
  return items_;
}

auto ItemDatabase::version() const noexcept -> uint8_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return version_;
}

auto ItemDatabase::size() const noexcept -> size_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return items_.size();
}

auto ItemDatabase::ready() const noexcept -> bool {
  std::lock_guard<std::mutex> lock{mutex_};
  return valid_;
}

void ItemDatabase::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  items_.clear();
  version_ = 0;
  valid_ = false;
}

auto item_database() -> ItemDatabase & { return ItemDatabase::instance(); }

auto item_name(uint32_t id) -> std::string {
  const auto item = ItemDatabase::instance().find(id);
  return item ? item->name : std::string{};
}

auto item_info(uint32_t id) -> std::optional<ItemInfo> {
  return ItemDatabase::instance().find(id);
}

} // namespace spx
