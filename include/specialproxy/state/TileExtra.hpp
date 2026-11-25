/** @file
 *  @brief Generic representation of Growtopia tile extra data.
 */

#ifndef SPXCORE_STATE_TILE_EXTRA_HPP
#define SPXCORE_STATE_TILE_EXTRA_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace spx::state {

/** Parsed contents of one tile extra block.
 *
 * The wire has a distinct layout for every extra type. This representation
 * keeps the type tag plus the values that fit a common shape, so scripts can
 * inspect doors, signs, locks, vending machines and similar tiles without a
 * separate type for each variant.
 */
struct TileExtra {
  uint8_t type{0}; ///< Wire type tag of the extra block.
  std::string text; ///< First text value, typically a label or player name.
  uint32_t value1{0}; ///< First numeric value of the block.
  uint32_t value2{0}; ///< Second numeric value of the block.
  std::vector<uint32_t> values; ///< Lists such as access ids or linked nodes.
  std::vector<std::pair<uint32_t, uint32_t>> pairs; ///< Item and amount pairs.
  bool parsed{false}; ///< True when the block was fully consumed.
};

} // namespace spx::state

#endif // SPXCORE_STATE_TILE_EXTRA_HPP
