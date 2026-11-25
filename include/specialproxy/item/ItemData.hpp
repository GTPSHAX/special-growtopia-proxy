/** @file
 *  @brief Public snapshot of item data received from the game server.
 */

#ifndef SPXCORE_ITEM_ITEM_DATA_HPP
#define SPXCORE_ITEM_ITEM_DATA_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spx {

/** Decompressed items.dat captured from the upstream server. */
struct ItemData {
  uint8_t version{0};            ///< Version byte from the items.dat header.
  uint32_t count{0};             ///< Number of items declared in the header.
  uint32_t checksum{0};          ///< CRC32 of the decompressed bytes.
  std::vector<std::byte> bytes;  ///< Complete decompressed items.dat body.
  bool valid{false};             ///< True when a valid upstream file was captured.
};

/**
 * @brief Read a snapshot of the latest upstream item data.
 *
 * The data is captured from the game protocol, never from a local file.
 * Consumers own persistence and decide where to write the bytes.
 *
 * @return Copy of the current item data, invalid before the first response.
 */
auto item_data() -> ItemData;

/**
 * @brief Test whether valid upstream item data has been received.
 *
 * @return True when a valid items.dat is held in memory.
 */
auto item_data_ready() noexcept -> bool;

} // namespace spx

#endif // SPXCORE_ITEM_ITEM_DATA_HPP
