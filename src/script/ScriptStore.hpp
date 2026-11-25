/** @file
 *  @brief Internal storage of pending Lua scripts.
 */

#ifndef SPXCORE_SCRIPT_SCRIPT_STORE_HPP
#define SPXCORE_SCRIPT_SCRIPT_STORE_HPP

#include <string>
#include <vector>

namespace spx::script {

/** One queued Lua source with its diagnostic label. */
struct ScriptEntry {
  std::string source; ///< Lua source code.
  std::string name;   ///< Label used in diagnostics.
};

/** @brief Take ownership of all queued scripts.
 *
 * @return Scripts in the order they were queued, leaving the queue empty.
 */
auto take_scripts() -> std::vector<ScriptEntry>;

/** @brief Read the configured auto-load directory.
 *
 * @return Folder path, never empty.
 */
auto script_directory() -> std::string;

} // namespace spx::script

#endif // SPXCORE_SCRIPT_SCRIPT_STORE_HPP
