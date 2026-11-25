#include "specialproxy/script/Script.hpp"

#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "ScriptStore.hpp"

namespace spx {

namespace {

std::mutex g_script_mutex;
std::vector<script::ScriptEntry> g_scripts;
std::string g_script_directory{"scripts"};

} // namespace

auto script_load(std::string_view source, std::string_view name) -> bool {
  std::lock_guard<std::mutex> lock(g_script_mutex);
  g_scripts.push_back(
      script::ScriptEntry{std::string{source}, std::string{name}});
  return true;
}

auto script_load_file(std::string_view path) -> bool {
  if (path.empty()) {
    return false;
  }
  std::ifstream input{std::string{path}, std::ios::binary};
  if (!input) {
    return false;
  }
  const std::string source{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    return false;
  }
  return script_load(source, path);
}

void script_set_directory(std::string_view path) {
  std::lock_guard<std::mutex> lock(g_script_mutex);
  g_script_directory.assign(path);
}

void script_clear() {
  std::lock_guard<std::mutex> lock(g_script_mutex);
  g_scripts.clear();
  g_script_directory.assign("scripts");
}

namespace script {

auto take_scripts() -> std::vector<ScriptEntry> {
  std::lock_guard<std::mutex> lock(g_script_mutex);
  std::vector<ScriptEntry> result;
  result.swap(g_scripts);
  return result;
}

auto script_directory() -> std::string {
  std::lock_guard<std::mutex> lock(g_script_mutex);
  return g_script_directory;
}

} // namespace script
} // namespace spx
