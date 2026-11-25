/** @file
 *  @brief Output channel available to command implementations.
 */

#ifndef SPXCORE_COMMAND_COMMAND_OUTPUT_HPP
#define SPXCORE_COMMAND_COMMAND_OUTPUT_HPP

#include <string_view>

namespace spx::command {

/** @brief Sends command replies back to the requesting game client. */
class CommandOutput {
 public:
  virtual ~CommandOutput() = default;

  /** @brief Send one line as a console message.
   *
   * @param[in] message Text shown in the client console area.
   * @return True when the frame was queued for delivery.
   */
  virtual auto send_console(std::string_view message) -> bool = 0;

  /** @brief Send one line as a plain game log message.
   *
   * @param[in] message Text shown in the client chat window.
   * @return True when the frame was queued for delivery.
   */
  virtual auto send_text(std::string_view message) -> bool = 0;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_COMMAND_OUTPUT_HPP
