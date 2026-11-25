/** @file
 *  @brief Aggregated game state with public access to its managers.
 */

#ifndef SPXCORE_STATE_GAME_STATE_HPP
#define SPXCORE_STATE_GAME_STATE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "specialproxy/state/PlayerManager.hpp"
#include "specialproxy/state/WorldManager.hpp"

namespace spx {

/** Owns the managers populated from observed game packets. */
class GameState {
 public:
  /** @brief Process wide game state. */
  static auto instance() -> GameState&;

  GameState(const GameState&) = delete;
  auto operator=(const GameState&) -> GameState& = delete;

  /** @brief Access the player roster.
   *
   * @return Manager holding observed players.
   */
  [[nodiscard]] auto players() -> state::PlayerManager&;

  /** @brief Access the tile map.
   *
   * @return Manager holding observed tiles.
   */
  [[nodiscard]] auto tiles() -> state::TileManager&;

  /** @brief Access world objects.
   *
   * @return Manager holding observed objects.
   */
  [[nodiscard]] auto objects() -> state::ObjectManager&;

  /** @brief Access non player characters.
   *
   * @return Manager holding observed NPCs.
   */
  [[nodiscard]] auto npcs() -> state::NpcManager&;

  /** @brief Read the net id marked as local by the last OnSpawn packet.
   *
   * @return Local net id, or -1 before a local spawn was observed.
   */
  [[nodiscard]] auto local_net_id() const noexcept -> int32_t;

  /** @brief Remember the avatar marked local by an OnSpawn packet.
   *
   * @param[in] net_id Net id reported by the local spawn.
   */
  void set_local_net_id(int32_t net_id) noexcept;

  /** @brief Feed one complete wire frame into every manager.
   *
   * @param[in] frame Complete ENet frame bytes.
   */
  void observe(std::span<const std::byte> frame);

  /** @brief Clear world identity and every tracked collection. */
  void reset();

 private:
  GameState() = default;

  state::PlayerManager players_;
  state::TileManager tiles_;
  state::ObjectManager objects_;
  state::NpcManager npcs_;
  int32_t local_net_id_{-1};
};

/** @brief Shortcut to the shared player manager. */
auto player_manager() -> state::PlayerManager&;

/** @brief Shortcut to the shared tile manager. */
auto tile_manager() -> state::TileManager&;

/** @brief Shortcut to the shared object manager. */
auto object_manager() -> state::ObjectManager&;

/** @brief Shortcut to the shared NPC manager. */
auto npc_manager() -> state::NpcManager&;

} // namespace spx

#endif // SPXCORE_STATE_GAME_STATE_HPP
