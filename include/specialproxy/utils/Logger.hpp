/** @file
 *  @brief Thread-safe diagnostic logger with levels and file sink.
 */

#ifndef SPXCORE_UTILS_LOGGER_HPP
#define SPXCORE_UTILS_LOGGER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wgcc-compat"
#endif

namespace spx::log {

/** Severity of a log record. */
enum class Level : uint8_t {
  Debug = 0,  ///< Detailed development information.
  Info = 1,   ///< Normal lifecycle events and decisions.
  Warning = 2, ///< Recoverable failures that need attention.
  Error = 3,  ///< Failures that stop an operation.
  Off = 4,    ///< Suppresses every record.
};

/** @brief Prefix for a level, used in the formatted record. */
auto level_name(Level level) -> std::string_view;

/**
 * @brief Process wide logger with a console sink and an optional file sink.
 *
 * The logger owns its mutex and file stream. Records are written completely
 * under the lock so lines from different threads never interleave.
 */
class Logger {
 public:
  /** @brief Access the single process wide logger. */
  static auto instance() -> Logger&;

  Logger(const Logger&) = delete;
  auto operator=(const Logger&) -> Logger& = delete;

  /**
   * @brief Set the minimum level that is emitted.
   *
   * @param level Minimum level, Off disables every record.
   */
  void set_level(Level level);

  /** @brief Current minimum level. */
  [[nodiscard]] auto level() const noexcept -> Level;

  /**
   * @brief Enable or disable the console sink, on by default.
   *
   * @param enabled True writes records to stdout.
   */
  void set_console_enabled(bool enabled);

  /**
   * @brief Configure the file sink.
   *
   * @param path Destination path, empty closes the current file.
   * @param append True appends to an existing file, false truncates it.
   * @return True when the file was opened or the sink was disabled.
   */
  auto set_file(const std::string& path, bool append = true) -> bool;

  /** @brief Current file sink path, empty when disabled. */
  [[nodiscard]] auto file_path() const -> std::string;

  /**
   * @brief Write one record.
   *
   * @param level Severity of the record.
   * @param module Short component name printed in brackets.
   * @param message Record text, already formatted by the caller.
   */
  void log(Level level, std::string_view module, std::string_view message);

  /**
   * @brief Write one printf formatted record.
   *
   * @tparam Args Format argument types.
   * @param level Severity of the record.
   * @param module Short component name printed in brackets.
   * @param format printf style format string.
   * @param args Values consumed by the format string.
   */
  template <typename... Args>
  void logf(Level level, std::string_view module, const char* format,
            Args&&... args) {
    log(level, module, format_message(format, args...));
  }

 private:
  Logger();
  ~Logger();

  /** @brief Render printf arguments into a bounded temporary string. */
  template <typename... Args>
  static auto format_message(const char* format, Args&&... args) -> std::string {
    char stack[512];
    const int count = std::snprintf(stack, sizeof stack, format, args...);
    if (count < 0) {
      return {};
    }
    const size_t needed = static_cast<size_t>(count);
    if (needed < sizeof stack) {
      return std::string{stack, needed};
    }
    std::string buffer(needed + 1, '\0');
    std::snprintf(buffer.data(), buffer.size(), format, args...);
    buffer.resize(needed);
    return buffer;
  }

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/** @brief Write a debug record. */
void debug(std::string_view module, std::string_view message);
/** @brief Write an info record. */
void info(std::string_view module, std::string_view message);
/** @brief Write a warning record. */
void warning(std::string_view module, std::string_view message);
/** @brief Write an error record. */
void error(std::string_view module, std::string_view message);

/** @brief Write a printf formatted debug record. */
template <typename... Args>
void debugf(std::string_view module, const char* format, Args&&... args) {
  Logger::instance().logf(Level::Debug, module, format, args...);
}
/** @brief Write a printf formatted info record. */
template <typename... Args>
void infof(std::string_view module, const char* format, Args&&... args) {
  Logger::instance().logf(Level::Info, module, format, args...);
}
/** @brief Write a printf formatted warning record. */
template <typename... Args>
void warningf(std::string_view module, const char* format, Args&&... args) {
  Logger::instance().logf(Level::Warning, module, format, args...);
}
/** @brief Write a printf formatted error record. */
template <typename... Args>
void errorf(std::string_view module, const char* format, Args&&... args) {
  Logger::instance().logf(Level::Error, module, format, args...);
}

} // namespace spx::log

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // SPXCORE_UTILS_LOGGER_HPP
