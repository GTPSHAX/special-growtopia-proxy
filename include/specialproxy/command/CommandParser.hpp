/** @file
 *  @brief Tokenization of prefixed chat command input.
 */

#ifndef SPXCORE_COMMAND_COMMAND_PARSER_HPP
#define SPXCORE_COMMAND_COMMAND_PARSER_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace spx::command {

/** Command name and arguments extracted from one chat line. */
struct ParsedCommand {
  std::string name;                ///< Command name without the prefix.
  std::vector<std::string> args;   ///< Arguments in declaration order.
};

/** Splits prefixed chat input into a command name and quoted arguments. */
class CommandParser {
 public:
  /** @brief Create a parser for one command prefix.
   *
   * @param[in] prefix Character that introduces a command.
   */
  explicit CommandParser(char prefix = '/') noexcept : prefix_{prefix} {}

  /** @brief Replace the command prefix.
   *
   * @param[in] prefix Character that introduces a command.
   */
  constexpr void set_prefix(char prefix) noexcept { prefix_ = prefix; }

  /** @brief Read the active command prefix.
   *
   * @return Character that introduces a command.
   */
  [[nodiscard]] constexpr auto prefix() const noexcept -> char {
    return prefix_;
  }

  /** @brief Test whether input starts with the command prefix.
   *
   * Leading whitespace is skipped before the prefix is examined.
   *
   * @param[in] input Raw chat input.
   * @return True when the input is a command candidate.
   */
  [[nodiscard]] auto is_command(std::string_view input) const noexcept -> bool;

  /** @brief Split one command line into a name and arguments.
   *
   * Double quotes group one argument and may contain spaces. An unterminated
   * quote makes the whole input malformed.
   *
   * @param[in] input Raw chat input.
   * @return Parsed command, or nullopt for non-command or malformed input.
   */
  [[nodiscard]] auto parse(std::string_view input) const
      -> std::optional<ParsedCommand>;

 private:
  char prefix_;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_COMMAND_PARSER_HPP
