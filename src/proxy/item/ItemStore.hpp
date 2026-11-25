/** @file
 *  @brief In-memory store for item data received from the game server.
 */

#ifndef SPXCORE_ITEM_ITEM_STORE_HPP
#define SPXCORE_ITEM_ITEM_STORE_HPP

#include <cstddef>
#include <mutex>
#include <span>

#include "specialproxy/item/ItemData.hpp"

namespace spx::item {

/** Owns the latest validated items.dat received from upstream. */
class ItemStore {
public:
  /** @brief Process wide item store. */
  static auto instance() -> ItemStore &;

  /** @brief Accept one compressed SendItemDatabaseData payload.
   *
   * @param[in] compressed Compressed bytes carried by the tank packet.
   * @return True when the data was decompressed, validated and stored.
   */
  auto accept(std::span<const std::byte> compressed) -> bool;

  /** @brief Record the server item hash and decide whether to fetch data.
   *
   * @param[in] hash Hash advertised by the OnSuperMainStart variant.
   * @return True when the caller should send action|refresh_item_data upstream.
   */
  auto note_upstream_hash(uint32_t hash) -> bool;

  /** @brief Copy the currently stored item data.
   *
   * @return Snapshot, invalid before the first accepted response.
   */
  [[nodiscard]] auto snapshot() const -> ItemData;

  /** @brief Test whether valid data has been stored.
   *
   * @return True after the first accepted response.
   */
  [[nodiscard]] auto ready() const noexcept -> bool;

private:
  ItemData current_;
  uint32_t expected_hash_{0};
  bool request_pending_{false};
  mutable std::mutex mutex_;
};

} // namespace spx::item

#endif // SPXCORE_ITEM_ITEM_STORE_HPP
