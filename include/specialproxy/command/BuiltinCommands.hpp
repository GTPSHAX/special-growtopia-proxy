/** @file
 *  @brief Registration of commands shipped with the proxy core.
 */

#ifndef SPXCORE_COMMAND_BUILTIN_COMMANDS_HPP
#define SPXCORE_COMMAND_BUILTIN_COMMANDS_HPP

#include "specialproxy/command/CommandRegistry.hpp"

namespace spx::command {

/** @brief Register the default command set on one registry.
 *
 * The set covers help, connectivity checks and session diagnostics.
 *
 * @param[in,out] registry Registry that receives the commands.
 */
void register_builtin_commands(CommandRegistry& registry);

} // namespace spx::command

#endif // SPXCORE_COMMAND_BUILTIN_COMMANDS_HPP
