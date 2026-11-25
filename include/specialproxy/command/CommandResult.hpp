/** @file
 *  @brief Outcomes produced by command parsing and execution.
 */

#ifndef SPXCORE_COMMAND_COMMAND_RESULT_HPP
#define SPXCORE_COMMAND_COMMAND_RESULT_HPP

namespace spx::command {

/** Result of dispatching one chat input through a command registry. */
enum class CommandResult {
  /** Input is ordinary chat and was not dispatched. */
  NotCommand,
  /** Input carried the prefix but named no registered command. */
  UnknownCommand,
  /** Command ran and completed its action. */
  Success,
  /** Command ran but its arguments were missing or malformed. */
  InvalidArguments,
  /** Command ran but could not complete its action. */
  Failed,
};

/** @brief Test whether an input was claimed by the command layer.
 *
 * @param[in] result Result of a dispatch call.
 * @return True for every result except NotCommand.
 */
[[nodiscard]] constexpr auto is_command_input(CommandResult result) noexcept
    -> bool {
  return result != CommandResult::NotCommand;
}

} // namespace spx::command

#endif // SPXCORE_COMMAND_COMMAND_RESULT_HPP
