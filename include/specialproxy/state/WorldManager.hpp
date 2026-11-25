/** @file
 *  @brief World metadata and the managers derived from it.
 */

#ifndef SPXCORE_STATE_WORLD_MANAGER_HPP
#define SPXCORE_STATE_WORLD_MANAGER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "specialproxy/item/ItemDatabase.hpp"
#include "specialproxy/state/EntityManager.hpp"
#include "specialproxy/state/TileExtra.hpp"

namespace spx::state {

/** Identity of the world currently being played. */
struct WorldInfo {
  std::string name;       ///< Name of the world.
  int32_t width{0};       ///< Tile columns reported by the map packet.
  int32_t height{0};      ///< Tile rows reported by the map packet.
  uint16_t version{0};    ///< Map format version.
  uint32_t tile_count{0}; ///< Number of tiles declared by the map packet.
};

/** One tile observed through a tile update packet. */
struct Tile {
  int32_t x{0};             ///< Horizontal tile coordinate.
  int32_t y{0};             ///< Vertical tile coordinate.
  uint16_t foreground{0};   ///< Foreground item id.
  uint16_t background{0};   ///< Background item id.
  uint16_t flags{0};        ///< Tile flag bits.
  TileExtra extra;          ///< Parsed tile extra block when present.
};

/** One world object. */
struct WorldObject {
  uint32_t object_id{0}; ///< Identifier assigned by the server.
  uint16_t item_id{0};   ///< Item identifier of the object.
  float x{0.0F};         ///< Horizontal world position.
  float y{0.0F};         ///< Vertical world position.
  uint8_t amount{0};     ///< Item count carried by the object.
  uint8_t flags{0};      ///< Object flag bits.
};

/** One non player character observed in the world. */
struct Npc {
  int32_t net_id{0};    ///< Network identifier of the NPC.
  uint32_t npc_id{0};   ///< NPC type identifier.
  float x{0.0F};        ///< Horizontal world position.
  float y{0.0F};        ///< Vertical world position.
  uint32_t flags{0};    ///< State flag bits.
};

/** Holds world identity shared by the derived collection managers. */
class WorldManager : public EntityManager {
 public:
  /** @brief Replace world identity.
   *
   * @param[in] info New world metadata.
   */
  void set_info(WorldInfo info);

  /** @brief Copy world identity.
   *
   * @return Metadata received from the latest map packet.
   */
  [[nodiscard]] auto info() const -> WorldInfo;

 protected:
  WorldInfo info_;
};

/** Stores tiles keyed by coordinate pair. */
class TileManager final : public WorldManager {
 public:
  /** @brief Insert or replace one tile.
   *
   * @param[in] tile Tile to store.
   */
  void upsert(Tile tile);

  /** @brief Find one tile.
   *
   * @param[in] x Horizontal coordinate.
   * @param[in] y Vertical coordinate.
   * @return Copy of the tile, or nullopt when absent.
   */
  [[nodiscard]] auto get(int32_t x, int32_t y) const -> std::optional<Tile>;

  /** @brief Copy every stored tile.
   *
   * @return Tiles in unspecified order.
   */
  [[nodiscard]] auto snapshot() const -> std::vector<Tile>;

  /** @brief Resolve the foreground item of one tile.
   *
   * @param[in] x Horizontal coordinate.
   * @param[in] y Vertical coordinate.
   * @return Item definition, or nullopt for an unknown tile or item.
   */
  [[nodiscard]] auto foreground_item(int32_t x, int32_t y) const
      -> std::optional<ItemInfo>;

  /** @brief Resolve the background item of one tile.
   *
   * @param[in] x Horizontal coordinate.
   * @param[in] y Vertical coordinate.
   * @return Item definition, or nullopt for an unknown tile or item.
   */
  [[nodiscard]] auto background_item(int32_t x, int32_t y) const
      -> std::optional<ItemInfo>;

  /** @brief Remove every tile. */
  void clear() override;

  /** @brief Count stored tiles.
   *
   * @return Number of tiles currently held.
   */
  [[nodiscard]] auto size() const noexcept -> size_t override;

 private:
  std::unordered_map<uint64_t, Tile> tiles_;
};

/** Stores world objects keyed by object identifier. */
class ObjectManager final : public WorldManager {
 public:
  /** @brief Insert or replace one object.
   *
   * @param[in] object Object to store.
   */
  void upsert(WorldObject object);

  /** @brief Remove one object.
   *
   * @param[in] object_id Identifier to remove.
   */
  void remove(uint32_t object_id);

  /** @brief Copy every stored object.
   *
   * @return Objects in unspecified order.
   */
  [[nodiscard]] auto snapshot() const -> std::vector<WorldObject>;

  /** @brief Replace the server drop id counter from map data.
   *
   * @param[in] value Drop id counter reported by the server.
   */
  void set_drop_id(uint32_t value) noexcept;

  /** @brief Allocate the next drop id for a locally observed drop.
   *
   * @return Newly assigned object identifier.
   */
  auto allocate_drop_id() -> uint32_t;

  /** @brief Remove every object. */
  void clear() override;

  /** @brief Count stored objects.
   *
   * @return Number of objects currently held.
   */
  [[nodiscard]] auto size() const noexcept -> size_t override;

 private:
  std::unordered_map<uint32_t, WorldObject> objects_;
  uint32_t drop_id_{0};
};

/** Stores NPCs keyed by network identifier. */
class NpcManager final : public WorldManager {
 public:
  /** @brief Insert or replace one NPC.
   *
   * @param[in] npc NPC to store.
   */
  void upsert(Npc npc);

  /** @brief Remove one NPC.
   *
   * @param[in] net_id Network identifier to remove.
   */
  void remove(int32_t net_id);

  /** @brief Copy every stored NPC.
   *
   * @return NPCs in unspecified order.
   */
  [[nodiscard]] auto snapshot() const -> std::vector<Npc>;

  /** @brief Remove every NPC. */
  void clear() override;

  /** @brief Count stored NPCs.
   *
   * @return Number of NPCs currently held.
   */
  [[nodiscard]] auto size() const noexcept -> size_t override;

 private:
  std::unordered_map<int32_t, Npc> npcs_;
};

} // namespace spx::state

#endif // SPXCORE_STATE_WORLD_MANAGER_HPP
