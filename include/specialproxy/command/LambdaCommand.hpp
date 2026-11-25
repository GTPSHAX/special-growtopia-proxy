/** @file
 *  @brief Adapter that turns a callable into a command implementation.
 */

#ifndef SPXCORE_COMMAND_LAMBDA_COMMAND_HPP
#define SPXCORE_COMMAND_LAMBDA_COMMAND_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "specialproxy/command/ICommand.hpp"

namespace spx::command {

/** Owns a callable and exposes it through the command interface. */
template <typename Func>
class LambdaCommand final : public ICommand {
 public:
  /** @brief Store the command metadata and callable.
   *
   * @param[in] name Registered command name.
   * @param[in] description Short help text.
   * @param[in] func Callable receiving a CommandContext.
   */
  LambdaCommand(std::string name, std::string description, Func func)
      : name_{std::move(name)},
        description_{std::move(description)},
        func_{std::move(func)} {}

  /** @brief Read the registered command name.
   *
   * @return Name without the command prefix.
   */
  [[nodiscard]] auto name() const -> std::string_view override {
    return name_;
  }

  /** @brief Read the short help text.
   *
   * @return Description shown by the help command.
   */
  [[nodiscard]] auto description() const -> std::string_view override {
    return description_;
  }

  /** @brief Invoke the stored callable.
   *
   * @param[in] context Name, arguments, output and session facts.
   * @return Outcome returned by the callable, or Success when it returns void.
   */
  auto execute(const CommandContext& context) -> CommandResult override {
    if constexpr (std::is_void_v<std::invoke_result_t<Func,
                                                     const CommandContext&>>) {
      func_(context);
      return CommandResult::Success;
    } else {
      return func_(context);
    }
  }

 private:
  std::string name_;
  std::string description_;
  Func func_;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_LAMBDA_COMMAND_HPP
