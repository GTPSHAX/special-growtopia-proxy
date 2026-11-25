/** @file
 *  @brief Hosts file operations shared by every platform.
 */

#include "specialproxy/platforms/Platform.hpp"

#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "PlatformError.hpp"
#include "specialproxy/platforms/detail/HostsBlock.hpp"

namespace spx::platform {

namespace {

std::mutex g_hosts_mutex;
std::mutex g_path_mutex;
std::string g_hosts_override;

/** Per thread error slot shared by every platform implementation. */
thread_local std::string g_last_error;

} // namespace

void detail::set_last_error(std::string message) {
  g_last_error = std::move(message);
}

void detail::set_hosts_path_override(std::string path) {
  std::lock_guard<std::mutex> lock(g_path_mutex);
  g_hosts_override = std::move(path);
}

/** @brief Resolve the active hosts file path, including the test override. */
auto hosts_file_path() -> std::string {
  std::lock_guard<std::mutex> lock(g_path_mutex);
  return g_hosts_override.empty() ? hosts_path() : g_hosts_override;
}

/** @brief Read a whole file into a string. */
auto read_file(std::string_view path, std::string &content) -> bool {
  FILE *file = std::fopen(std::string{path}.c_str(), "rb");
  if (file == nullptr) {
    detail::set_last_error("cannot open " + std::string{path});
    return false;
  }
  char buffer[4096];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    content.append(buffer, read);
  }
  const bool ok = std::ferror(file) == 0;
  std::fclose(file);
  if (!ok) {
    detail::set_last_error("read failed on " + std::string{path});
  }
  return ok;
}

/** @brief Write a whole file, replacing its content. */
auto write_file(std::string_view path, std::string_view content) -> bool {
  FILE *file = std::fopen(std::string{path}.c_str(), "wb");
  if (file == nullptr) {
    detail::set_last_error("cannot write " + std::string{path});
    return false;
  }
  const size_t written = std::fwrite(content.data(), 1, content.size(), file);
  std::fclose(file);
  if (written != content.size()) {
    detail::set_last_error("write failed on " + std::string{path});
    return false;
  }
  return true;
}

auto last_error() -> std::string_view { return g_last_error; }

auto list_host_entries() -> std::vector<HostEntry> {
  std::string content;
  if (!read_file(hosts_file_path(), content)) {
    return {};
  }
  const auto split = detail::split_block(content);
  return detail::parse_block(split.body).entries;
}

auto add_host_entries(std::span<const HostEntry> entries) -> bool {
  std::lock_guard<std::mutex> lock(g_hosts_mutex);
  std::string content;
  if (!read_file(hosts_file_path(), content)) {
    return false;
  }

  std::vector<std::string_view> hostnames;
  hostnames.reserve(entries.size());
  for (const auto &entry : entries) {
    hostnames.push_back(entry.hostname);
  }
  const auto disabled = detail::disable_conflicts(content, hostnames);
  auto split = detail::split_block(disabled.content);
  auto data = detail::parse_block(split.body);
  data.disabled.insert(data.disabled.end(), disabled.originals.begin(),
                       disabled.originals.end());
  bool changed = !disabled.originals.empty();
  for (const auto &requested : entries) {
    bool found = false;
    for (auto &entry : data.entries) {
      if (detail::same_hostname(entry.hostname, requested.hostname)) {
        found = true;
        if (entry.ip != requested.ip) {
          entry.ip = requested.ip;
          changed = true;
        }
        break;
      }
    }
    if (!found) {
      data.entries.push_back(requested);
      changed = true;
    }
  }
  if (!changed) {
    return true;
  }
  return write_file(hosts_file_path(), detail::join(split, data));
}

auto add_host_entry(std::string_view hostname, std::string_view ip) -> bool {
  const HostEntry entry{std::string{hostname}, std::string{ip}};
  return add_host_entries(std::span<const HostEntry>{&entry, 1});
}

auto remove_host_entries() -> bool {
  std::lock_guard<std::mutex> lock(g_hosts_mutex);
  std::string content;
  if (!read_file(hosts_file_path(), content)) {
    return false;
  }

  auto split = detail::split_block(content);
  if (!split.block_present) {
    return true;
  }
  const auto data = detail::parse_block(split.body);
  return write_file(hosts_file_path(), detail::restore_block(split, data));
}

} // namespace spx::platform
