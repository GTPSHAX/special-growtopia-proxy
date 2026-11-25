/** @file
 *  @brief Logger implementation with timestamped console and file output.
 */

#include "specialproxy/utils/Logger.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <utility>

namespace spx::log {

struct Logger::Impl {
  std::mutex mutex;
  Level level{Level::Info};
  bool console_enabled{true};
  std::string path;
  std::ofstream file;
};

namespace {

/** @brief Format the current local time as YYYY-MM-DD HH:MM:SS.mmm. */
auto timestamp() -> std::string {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds)
          .count();
  const std::time_t raw = clock::to_time_t(seconds);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &raw);
#else
  localtime_r(&raw, &local);
#endif
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec,
                static_cast<long long>(millis));
  return std::string{buffer};
}

} // namespace

auto level_name(Level level) -> std::string_view {
  switch (level) {
  case Level::Debug:
    return "DEBUG";
  case Level::Info:
    return "INFO ";
  case Level::Warning:
    return "WARN ";
  case Level::Error:
    return "ERROR";
  case Level::Off:
    return "OFF  ";
  }
  return "?????";
}

Logger::Logger() : impl_(std::make_unique<Impl>()) {}

Logger::~Logger() = default;

auto Logger::instance() -> Logger & {
  static Logger logger;
  return logger;
}

void Logger::set_level(Level level) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->level = level;
}

auto Logger::level() const noexcept -> Level {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->level;
}

void Logger::set_console_enabled(bool enabled) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->console_enabled = enabled;
}

auto Logger::set_file(const std::string &path, bool append) -> bool {
  const auto mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
  std::ofstream file;
  if (!path.empty()) {
    file.open(path, mode);
    if (!file.is_open()) {
      return false;
    }
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->file = std::move(file);
  impl_->path = path;
  return true;
}

auto Logger::file_path() const -> std::string {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->path;
}

void Logger::log(Level level, std::string_view module,
                 std::string_view message) {
  const std::string line =
      "[" + timestamp() + "] [" + std::string{level_name(level)} + "] [" +
      std::string{module} + "] " + std::string{message} + "\n";
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (level < impl_->level || impl_->level == Level::Off) {
    return;
  }
  if (impl_->console_enabled) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
  }
  if (impl_->file.is_open()) {
    impl_->file.write(line.data(), static_cast<std::streamsize>(line.size()));
    impl_->file.flush();
  }
}

void debug(std::string_view module, std::string_view message) {
  Logger::instance().log(Level::Debug, module, message);
}
void info(std::string_view module, std::string_view message) {
  Logger::instance().log(Level::Info, module, message);
}
void warning(std::string_view module, std::string_view message) {
  Logger::instance().log(Level::Warning, module, message);
}
void error(std::string_view module, std::string_view message) {
  Logger::instance().log(Level::Error, module, message);
}

} // namespace spx::log
