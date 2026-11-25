/** @file
 *  @brief Interface implemented by every chat command.
 */

#ifndef SPXCORE_COMMAND_I_COMMAND_HPP
#define SPXCORE_COMMAND_I_COMMAND_HPP

#include <string_view>

#include "specialproxy/command/CommandContext.hpp"
#include "specialproxy/command/CommandResult.hpp"

namespace spx::command {

/** One named action that can be invoked from game chat. */
class ICommand {
 public:
  virtual ~ICommand() = default;

  /** @brief Read the registered command name.
   *
   * @return Name without the command prefix.
   */
  [[nodiscard]] virtual auto name() const -> std::string_view = 0;

  /** @brief Read the short help text.
   *
   * @return Description shown by the help command.
   */
  [[nodiscard]] virtual auto description() const -> std::string_view = 0;

  /** @brief Read the argument syntax.
   *
   * @return Usage suffix, empty when the command takes no arguments.
   */
  [[nodiscard]] virtual auto usage() const -> std::string_view {
    return {};
  }

  /** @brief Run the command for one invocation.
   *
   * @param[in] context Name, arguments, output and session facts.
   * @return Outcome of the invocation.
   */
  virtual auto execute(const CommandContext& context) -> CommandResult = 0;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_I_COMMAND_HPP
