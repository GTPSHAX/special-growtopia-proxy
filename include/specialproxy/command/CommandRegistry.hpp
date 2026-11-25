/** @file
 *  @brief Ownership and dispatch of named chat commands.
 */

#ifndef SPXCORE_COMMAND_COMMAND_REGISTRY_HPP
#define SPXCORE_COMMAND_COMMAND_REGISTRY_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "specialproxy/command/CommandContext.hpp"
#include "specialproxy/command/CommandOutput.hpp"
#include "specialproxy/command/CommandParser.hpp"
#include "specialproxy/command/CommandResult.hpp"
#include "specialproxy/command/ICommand.hpp"
#include "specialproxy/command/LambdaCommand.hpp"

namespace spx::command {

/** Owns command implementations and dispatches prefixed chat input. */
class CommandRegistry {
 public:
  /** @brief Create an empty registry.
   *
   * @param[in] prefix Character that introduces a command.
   */
  explicit CommandRegistry(char prefix = '/') : parser_{prefix} {}

  /** @brief Replace the command prefix.
   *
   * @param[in] prefix Character that introduces a command.
   */
  void set_prefix(char prefix) { parser_.set_prefix(prefix); }

  /** @brief Read the active command prefix.
   *
   * @return Character that introduces a command.
   */
  [[nodiscard]] auto prefix() const noexcept -> char {
    return parser_.prefix();
  }

  /** @brief Transfer ownership of one command to the registry.
   *
   * @param[in] command Command implementation to register.
   */
  void add(std::unique_ptr<ICommand> command);

  /** @brief Remove one command by its registered name.
   *
   * @param[in] name Command name without the prefix.
   * @return True when a command was removed.
   */
  auto remove(std::string_view name) -> bool;

  /** @brief Register a command backed by a callable.
   *
   * @tparam Func Callable type receiving a CommandContext.
   * @param[in] name Registered command name.
   * @param[in] description Short help text.
   * @param[in] func Command callback.
   */
  template <typename Func>
  void add(std::string name, std::string description, Func&& func) {
    add(std::make_unique<LambdaCommand<std::decay_t<Func>>>(
        std::move(name), std::move(description), std::forward<Func>(func)));
  }

  /** @brief Find a registered command by name.
   *
   * @param[in] name Command name without the prefix.
   * @return Matching command, or null when absent.
   */
  [[nodiscard]] auto find(std::string_view name) const -> ICommand*;

  /** @brief List registered commands for help output.
   *
   * @return Pairs of command name and description.
   */
  [[nodiscard]] auto all() const
      -> std::vector<std::pair<std::string, std::string>>;

  /** @brief Count registered commands.
   *
   * @return Number of owned command implementations.
   */
  [[nodiscard]] auto size() const noexcept -> size_t {
    return commands_.size();
  }

  /** @brief Test whether input starts with the command prefix.
   *
   * @param[in] input Raw chat input.
   * @return True when the input is a command candidate.
   */
  [[nodiscard]] auto is_command(std::string_view input) const noexcept
      -> bool {
    return parser_.is_command(input);
  }

  /** @brief Parse and execute one chat input.
   *
   * The environment is extended with this registry before the command runs
   * so help can enumerate the available commands.
   *
   * @param[in] input Raw chat input.
   * @param[in] output Channel used to reply to the client.
   * @param[in] environment Facts about the triggering session.
   * @return Outcome of the dispatch.
   */
  auto dispatch(std::string_view input, CommandOutput& output,
                const CommandEnvironment& environment) const -> CommandResult;

 private:
  CommandParser parser_;
  std::unordered_map<std::string, std::unique_ptr<ICommand>> commands_;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_COMMAND_REGISTRY_HPP
