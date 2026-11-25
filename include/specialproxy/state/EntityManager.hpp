/** @file
 *  @brief Common base for all in-memory game state managers.
 */

#ifndef SPXCORE_STATE_ENTITY_MANAGER_HPP
#define SPXCORE_STATE_ENTITY_MANAGER_HPP

#include <cstddef>
#include <mutex>

namespace spx::state {

/** Shared interface and storage lock for state managers. */
class EntityManager {
 public:
  virtual ~EntityManager() = default;

  EntityManager(const EntityManager&) = delete;
  auto operator=(const EntityManager&) -> EntityManager& = delete;

  /** @brief Remove every tracked entry. */
  virtual void clear() = 0;

  /** @brief Count tracked entries.
   *
   * @return Number of entries currently held.
   */
  [[nodiscard]] virtual auto size() const noexcept -> size_t = 0;

  /** @brief Test whether the manager holds no entries.
   *
   * @return True when size() is zero.
   */
  [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }

 protected:
  EntityManager() = default;
  mutable std::mutex mutex_; ///< Guards derived collections.
};

} // namespace spx::state

#endif // SPXCORE_STATE_ENTITY_MANAGER_HPP
