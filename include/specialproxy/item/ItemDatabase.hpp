/** @file
 *  @brief In-memory database parsed from upstream items.dat.
 */

#ifndef SPXCORE_ITEM_ITEM_DATABASE_HPP
#define SPXCORE_ITEM_ITEM_DATABASE_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spx {

/** One item definition decoded from items.dat. */
struct ItemInfo {
  uint32_t id{0};                 ///< Item identifier.
  uint16_t flags{0};             ///< Primary flag bits.
  uint8_t type{0};               ///< Item type identifier.
  uint8_t material{0};           ///< Material identifier.
  std::string name;              ///< Decrypted display name.
  std::string texture_path;      ///< Texture file path.
  uint32_t texture_hash{0};      ///< Texture file hash.
  uint8_t visual_effect{0};      ///< Visual effect identifier.
  uint32_t cooking_time{0};      ///< Cooking time in milliseconds.
  uint8_t texture_x{0};          ///< Texture atlas column.
  uint8_t texture_y{0};          ///< Texture atlas row.
  uint8_t texture_type{0};       ///< Texture layout type.
  uint8_t stripey_wallpaper{0};  ///< Set for stripey wallpaper items.
  uint8_t collision_type{0};     ///< Collision identifier.
  uint8_t health{0};             ///< Tile health.
  uint32_t reset_time{0};        ///< Reset time.
  uint8_t clothing_type{0};      ///< Clothing slot identifier.
  uint16_t rarity{0};            ///< Rarity value.
  uint8_t max_amount{0};         ///< Maximum stack size.
  std::string extra_file;        ///< Extra asset path.
  uint32_t extra_file_hash{0};   ///< Extra asset hash.
  uint32_t animation_time{0};    ///< Animation timing.
  std::string pet_name;          ///< Pet name template.
  std::string pet_prefix;        ///< Pet name prefix.
  std::string pet_suffix;        ///< Pet name suffix.
  std::string pet_ability;       ///< Pet ability text.
  uint8_t seed_base{0};          ///< Seed base pattern identifier.
  uint8_t seed_overlay{0};       ///< Seed overlay pattern identifier.
  uint8_t tree_base{0};          ///< Tree base pattern identifier.
  uint8_t tree_leaves{0};        ///< Tree leaves pattern identifier.
  uint32_t seed_color{0};        ///< Seed tint color.
  uint32_t seed_overlay_color{0}; ///< Seed overlay tint color.
  uint32_t ingredient{0};        ///< Crafting ingredient id.
  uint32_t grow_time{0};         ///< Growth time.
  uint32_t tile_range{0};        ///< Tile range.
  uint32_t vault_capacity{0};    ///< Vault capacity.
  std::string punch_options;     ///< Punch behaviour options.
  uint32_t light_range{0};       ///< Light radius.
  std::string info;              ///< Item description text.
};

/** Owns item definitions parsed from a single upstream items.dat. */
class ItemDatabase {
 public:
  /** @brief Process wide item database. */
  static auto instance() -> ItemDatabase&;

  ItemDatabase(const ItemDatabase&) = delete;
  auto operator=(const ItemDatabase&) -> ItemDatabase& = delete;

  /** @brief Parse one uncompressed items.dat body.
   *
   * @param[in] data Uncompressed items.dat bytes.
   * @return True when the header and every item record were consumed.
   */
  auto parse(std::span<const std::byte> data) -> bool;

  /** @brief Find one item definition.
   *
   * @param[in] id Item identifier.
   * @return Copy of the definition, or nullopt when not present.
   */
  [[nodiscard]] auto find(uint32_t id) const -> std::optional<ItemInfo>;

  /** @brief Copy every item definition.
   *
   * @return Definitions in identifier order.
   */
  [[nodiscard]] auto snapshot() const -> std::vector<ItemInfo>;

  /** @brief Read the parsed file version.
   *
   * @return Version byte from the items.dat header.
   */
  [[nodiscard]] auto version() const noexcept -> uint8_t;

  /** @brief Count parsed items.
   *
   * @return Number of item definitions held.
   */
  [[nodiscard]] auto size() const noexcept -> size_t;

  /** @brief Test whether a valid file was parsed.
   *
   * @return True after the first successful parse.
   */
  [[nodiscard]] auto ready() const noexcept -> bool;

  /** @brief Remove all parsed items. */
  void clear();

 private:
  ItemDatabase() = default;

  mutable std::mutex mutex_;
  std::vector<ItemInfo> items_;
  uint8_t version_{0};
  bool valid_{false};
};

/** @brief Shortcut to the shared item database. */
auto item_database() -> ItemDatabase&;

/**
 * @brief Resolve one item id to its display name.
 *
 * @param[in] id Item identifier.
 * @return Decrypted item name, empty when the id is unknown.
 */
auto item_name(uint32_t id) -> std::string;

/**
 * @brief Resolve one item id to its full definition.
 *
 * @param[in] id Item identifier.
 * @return Item definition, or nullopt when the id is unknown.
 */
auto item_info(uint32_t id) -> std::optional<ItemInfo>;

} // namespace spx

#endif // SPXCORE_ITEM_ITEM_DATABASE_HPP
