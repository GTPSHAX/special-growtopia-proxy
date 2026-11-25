/** @file
 *  @brief Logger levels, file sink and timestamp format.
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "specialproxy/utils/Logger.hpp"

namespace {

using spx::log::Level;
using spx::log::Logger;

/** @brief Read a whole file into a string. */
auto read_all(const std::string &path) -> std::string {
  std::ifstream in{path, std::ios::binary};
  std::ostringstream stream;
  stream << in.rdbuf();
  return stream.str();
}

/** @brief Check that a line starts with a bracketed timestamp. */
auto has_timestamp(const std::string &line) -> bool {
  if (line.size() < 24 || line.front() != '[' || line[11] != ' ') {
    return false;
  }
  const std::string digits = "0123456789";
  for (const size_t at : {1u, 2u, 3u, 4u, 6u, 7u, 9u, 10u, 12u, 13u, 15u, 16u,
                          18u, 19u, 21u, 22u, 23u}) {
    if (digits.find(line[at]) == std::string::npos) {
      return false;
    }
  }
  return line[5] == '-' && line[8] == '-' && line[14] == ':' &&
         line[17] == ':' && line[20] == '.';
}

/** @brief Levels, append behavior and formatted records. */
auto test_logger() -> void {
  const std::string path =
      std::string{std::getenv("TEMP") ? std::getenv("TEMP") : "."} +
      "\\spx-logger-test.log";
  Logger &logger = Logger::instance();
  logger.set_console_enabled(false);
  logger.set_level(Level::Debug);
  assert(logger.set_file(path, false));
  assert(logger.file_path() == path);

  spx::log::infof("test", "value=%d", 42);
  spx::log::warning("test", "careful");
  spx::log::error("test", "broken");
  std::string content = read_all(path);
  assert(content.find("[INFO ] [test] value=42") != std::string::npos);
  assert(content.find("[WARN ] [test] careful") != std::string::npos);
  assert(content.find("[ERROR] [test] broken") != std::string::npos);

  const size_t before = content.size();
  assert(logger.set_file(path, true));
  logger.set_level(Level::Warning);
  spx::log::infof("test", "filtered=%d", 7);
  spx::log::warning("test", "second");
  content = read_all(path);
  assert(content.find("filtered=7") == std::string::npos);
  assert(content.size() > before);
  assert(content.find("second") != std::string::npos);

  std::istringstream lines{content};
  std::string line;
  size_t count{0};
  while (std::getline(lines, line)) {
    ++count;
    assert(has_timestamp(line));
  }
  assert(count == 4);

  logger.set_file({});
  logger.set_console_enabled(true);
  logger.set_level(Level::Info);
  assert(logger.file_path().empty());
  std::remove(path.c_str());
}

} // namespace

int main() {
  test_logger();
  return 0;
}
