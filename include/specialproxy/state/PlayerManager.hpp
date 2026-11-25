/** @file
 *  @brief Player roster captured from server packets.
 */

#ifndef SPXCORE_STATE_PLAYER_MANAGER_HPP
#define SPXCORE_STATE_PLAYER_MANAGER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "specialproxy/state/EntityManager.hpp"

namespace spx::state {

/** One item stack held by the local player. */
struct InventoryItem {
  uint32_t id{0};    ///< Item identifier.
  uint8_t count{0};  ///< Number of items in the stack.
  uint8_t flags{0};  ///< Item flag bits.
};

/** One avatar observed in the current world. */
struct Player {
  int32_t net_id{-1};     ///< Network identifier used by the game protocol.
  int32_t user_id{0};     ///< GrowID numeric identifier.
  std::string name;       ///< Display name of the avatar.
  std::string country;    ///< Country code supplied by the server.
  float x{0.0F};          ///< Horizontal world position.
  float y{0.0F};          ///< Vertical world position.
  bool facing_left{false};///< Set when the avatar faces left.
  std::vector<InventoryItem> inventory; ///< Items carried by this avatar.
};

/** Owns the player roster keyed by network identifier. */
class PlayerManager final : public EntityManager {
 public:
  /** @brief Insert or replace one player.
   *
   * @param[in] player Player snapshot to store.
   */
  void upsert(Player player);

  /** @brief Remove one player by network identifier.
   *
   * @param[in] net_id Network identifier to remove.
   */
  void remove(int32_t net_id);

  /** @brief Find one player.
   *
   * @param[in] net_id Network identifier to look up.
   * @return Copy of the player, or nullopt when absent.
   */
  [[nodiscard]] auto find(int32_t net_id) const -> std::optional<Player>;

  /** @brief Copy the whole roster.
   *
   * @return Players in unspecified order.
   */
  [[nodiscard]] auto snapshot() const -> std::vector<Player>;

  /** @brief Copy the first tracked player.
   *
   * @return First player, or nullopt for an empty roster.
   */
  [[nodiscard]] auto first() const -> std::optional<Player>;

  /** @brief Replace the local player inventory.
   *
   * @param[in] version Wire version of the inventory packet.
   * @param[in] slot_count Number of inventory slots.
   * @param[in] items Item stacks to store.
   */
  void set_inventory(uint8_t version, uint32_t slot_count,
                     std::vector<InventoryItem> items);

  /** @brief Apply an incremental inventory change.
   *
   * @param[in] item_id Item identifier to adjust.
   * @param[in] delta Signed change to the item count.
   */
  void modify_inventory(int32_t item_id, int32_t delta);

  /** @brief Copy the local player inventory.
   *
   * @return Item stacks in declaration order.
   */
  [[nodiscard]] auto inventory() const -> std::vector<InventoryItem>;

  /** @brief Copy every inventory slot including empty ones.
   *
   * @return Slot count sized list, id zero marks an empty slot.
   */
  [[nodiscard]] auto inventory_slots() const -> std::vector<InventoryItem>;

  /** @brief Read the reported inventory slot count.
   *
   * @return Number of slots, zero before the first inventory packet.
   */
  [[nodiscard]] auto inventory_slot_count() const noexcept -> uint32_t;

  /** @brief Remove every player. */
  void clear() override;

  /** @brief Count tracked players.
   *
   * @return Number of players currently held.
   */
  [[nodiscard]] auto size() const noexcept -> size_t override;

 private:
  std::unordered_map<int32_t, Player> players_;
  uint8_t inventory_version_{0};
  uint32_t inventory_slot_count_{0};
  std::vector<InventoryItem> inventory_;
};

} // namespace spx::state

#endif // SPXCORE_STATE_PLAYER_MANAGER_HPP
