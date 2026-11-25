/** @file
 *  @brief Binary parsers for world map, tile and inventory payloads.
 */

#ifndef SPXCORE_SCRIPT_WORLD_PARSER_HPP
#define SPXCORE_SCRIPT_WORLD_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "specialproxy/state/GameState.hpp"

namespace spx::state {

/** Parsed contents of one SendMapData payload. */
struct WorldParseResult {
  WorldInfo info;                   ///< World identity from the header.
  std::vector<Tile> tiles;          ///< Tiles decoded from the map body.
  std::vector<WorldObject> objects; ///< Objects decoded from the map body.
  uint32_t drop_id{0};              ///< Server drop id counter for new objects.
  bool complete{false};             ///< True when every section was parsed.
};

/** @brief Parse one SendMapData extra payload.
 *
 * @param[in] extra Compressed-free map body carried by the tank packet.
 * @return Parsed sections, complete false when the body is malformed.
 */
auto parse_world_data(std::span<const std::byte> extra) -> WorldParseResult;

/** @brief Parse tile records carried by a tile update packet.
 *
 * @param[in] extra Tile records from the extra field.
 * @param[in] header_x Tile x from the header for a single update, ignored for
 * batches.
 * @param[in] header_y Tile y from the header for a single update, ignored for
 * batches.
 * @return Tiles decoded from the payload.
 */
auto parse_tile_updates(std::span<const std::byte> extra, int32_t header_x,
                        int32_t header_y) -> std::vector<Tile>;

} // namespace spx::state

#endif // SPXCORE_SCRIPT_WORLD_PARSER_HPP
