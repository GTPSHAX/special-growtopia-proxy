#include "LuaEngine.hpp"

extern "C" {
#include <lualib.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Scheduler.hpp"
#include "net/HttpClient.hpp"
#include "specialproxy/item/ItemData.hpp"
#include "specialproxy/item/ItemDatabase.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/platforms/Platform.hpp"
#include "specialproxy/state/GameState.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::script {

struct LuaEngine::Impl {
  struct DialogBlock {
    std::string pattern;
    std::regex compiled;
    int32_t remaining{0};
  };

  struct PendingCommand {
    int coroutine_ref{LUA_NOREF};
    std::chrono::steady_clock::time_point due;
    std::shared_ptr<ScriptHost> host;
    uint32_t session{0};
    uint64_t http_id{0};
  };

  lua_State *state{nullptr};
  std::unordered_map<std::string, std::vector<int>> hooks;
  std::unordered_map<std::string, int> commands;
  bool reload_requested{false};
  std::mutex dialog_mutex;
  std::vector<DialogBlock> dialog_blocks;
  std::mutex pending_mutex;
  std::vector<PendingCommand> pending;
  struct HttpSlot {
    std::mutex mutex;
    std::condition_variable ready;
    bool done{false};
    net::HttpResult result;
  };
  std::unordered_map<uint64_t, std::shared_ptr<HttpSlot>> http_slots;
  struct DeferredCallback {
    int callback_ref{LUA_NOREF};
    std::chrono::steady_clock::time_point due;
    std::shared_ptr<ScriptHost> host;
    uint32_t session{0};
  };
  std::mutex deferred_mutex;
  std::vector<DeferredCallback> deferred_callbacks;
  std::unique_ptr<Scheduler> scheduler;
  std::mutex http_config_mutex;
  net::Socks5Config http_socks5;
  bool http_socks5_enabled{false};
  ScriptHost *host{nullptr};
  uint32_t session{0};
  std::string version;
};

namespace {

constexpr const char *kEngineKey = "spx.engine";
constexpr int kInstructionLimit = 200000;

/** Canonical hook names indexed by the Mori style numeric constants. */
constexpr std::array<std::string_view, 10> kEventNames{
    "clientpacket", "serverpacket", "variant",  "gamepacket", "gamemessage",
    "connect",      "disconnect",   "redirect", "command",    "inventory"};

/** Map a hook name to its canonical form, including Growlauncher aliases. */
auto canonical_name(std::string_view name) -> std::string {
  std::string out;
  out.reserve(name.size() + 3);
  for (const char c : name) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  if (out.starts_with("on") && out.size() > 2) {
    out.erase(0, 2);
  }
  if (out == "sendpacket" || out == "sendpacketraw") {
    return "clientpacket";
  }
  return out;
}

/** Resolve a numeric event constant to its canonical name. */
auto event_name(int type) -> std::string_view {
  if (type < 1 || static_cast<size_t>(type) > kEventNames.size()) {
    return {};
  }
  return kEventNames[static_cast<size_t>(type - 1)];
}

/** @brief Register or extend one dialog block pattern. */
auto add_dialog_block_to(LuaEngine::Impl &impl, std::string_view pattern,
                         int32_t count) -> bool {
  if (count == 0) {
    return true;
  }
  try {
    std::regex compiled{std::string{pattern}, std::regex::optimize};
    std::lock_guard<std::mutex> lock{impl.dialog_mutex};
    for (auto &block : impl.dialog_blocks) {
      if (block.pattern != pattern) {
        continue;
      }
      if (count < 0) {
        block.remaining = -1;
      } else if (block.remaining >= 0) {
        block.remaining += count;
      }
      return true;
    }
    impl.dialog_blocks.push_back(LuaEngine::Impl::DialogBlock{
        std::string{pattern}, std::move(compiled), count});
    return true;
  } catch (const std::regex_error &error) {
    spx::log::warningf("script", "invalid dialog block regex '%s': %s",
                       std::string{pattern}.c_str(), error.what());
    return false;
  }
}

/** @brief Consume one block for a dialog body that matches a pattern. */
auto consume_dialog_block_from(LuaEngine::Impl &impl, std::string_view dialog)
    -> bool {
  std::lock_guard<std::mutex> lock{impl.dialog_mutex};
  for (auto it = impl.dialog_blocks.begin(); it != impl.dialog_blocks.end();
       ++it) {
    if (!std::regex_search(dialog.begin(), dialog.end(), it->compiled)) {
      continue;
    }
    if (it->remaining >= 0 && --it->remaining <= 0) {
      impl.dialog_blocks.erase(it);
    }
    return true;
  }
  return false;
}

thread_local int g_instruction_budget = 0;

/** Abort scripts that run longer than one hook budget. */
void instruction_hook(lua_State *state, lua_Debug *) {
  if (--g_instruction_budget <= 0) {
    luaL_error(state, "script instruction limit exceeded");
  }
}

/** Retrieve the engine bound to one Lua state. */
auto engine_of(lua_State *state) -> LuaEngine::Impl * {
  lua_getfield(state, LUA_REGISTRYINDEX, kEngineKey);
  auto *engine = static_cast<LuaEngine::Impl *>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return engine;
}

/** Run one Lua call with an instruction budget and report errors. */
auto protected_call(lua_State *state, int args, int results,
                    std::string_view context) -> bool {
  g_instruction_budget = kInstructionLimit;
  lua_sethook(state, instruction_hook, LUA_MASKCOUNT, 1000);
  const int status = lua_pcall(state, args, results, 0);
  lua_sethook(state, nullptr, 0, 0);
  if (status != LUA_OK) {
    const char *message = lua_tostring(state, -1);
    spx::log::warningf("script", "%s failed - %s", std::string{context}.c_str(),
                       message != nullptr ? message : "unknown error");
    lua_pop(state, 1);
    return false;
  }
  return true;
}

/** Resume one coroutine with an instruction budget and report errors. */
auto run_coroutine(lua_State *coroutine, int args, std::string_view context)
    -> int {
  g_instruction_budget = kInstructionLimit;
  lua_sethook(coroutine, instruction_hook, LUA_MASKCOUNT, 1000);
  int results = 0;
  const int status = lua_resume(coroutine, nullptr, args, &results);
  lua_sethook(coroutine, nullptr, 0, 0);
  if (status != LUA_OK && status != LUA_YIELD) {
    const char *message = lua_tostring(coroutine, -1);
    spx::log::warningf("script", "%s failed - %s", std::string{context}.c_str(),
                       message != nullptr ? message : "unknown error");
    lua_pop(coroutine, 1);
  }
  return status;
}

/** Convert the top Lua value into a packet hook decision. */
auto read_decision(lua_State *state, HookReply &reply) -> void {
  const int type = lua_type(state, -1);
  if (type == LUA_TBOOLEAN && lua_toboolean(state, -1) == 0) {
    reply.decision = HookDecision::Drop;
  } else if (type == LUA_TSTRING) {
    size_t length = 0;
    const char *bytes = lua_tolstring(state, -1, &length);
    reply.frame.assign(reinterpret_cast<const std::byte *>(bytes),
                       reinterpret_cast<const std::byte *>(bytes) + length);
    reply.decision = HookDecision::Replaced;
  }
  lua_pop(state, 1);
}

/** Push bytes passed from Lua as a binary string argument. */
auto lua_bytes(lua_State *state, int index, std::vector<std::byte> &out)
    -> bool {
  if (lua_type(state, index) != LUA_TSTRING) {
    return false;
  }
  size_t length = 0;
  const char *bytes = lua_tolstring(state, index, &length);
  out.assign(reinterpret_cast<const std::byte *>(bytes),
             reinterpret_cast<const std::byte *>(bytes) + length);
  return true;
}

/** @brief Read a deferred yield into one pending task. */
auto read_defer(lua_State *state, LuaEngine::Impl::PendingCommand &task)
    -> void {
  int64_t delay{0};
  if (lua_istable(state, -1)) {
    lua_getfield(state, -1, "httpId");
    task.http_id = static_cast<uint64_t>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, -1, "delay");
    delay = static_cast<int64_t>(lua_tointeger(state, -1));
    lua_pop(state, 1);
  } else {
    delay = static_cast<int64_t>(lua_tointeger(state, -1));
  }
  lua_pop(state, 1);
  task.due = std::chrono::steady_clock::now() +
             std::chrono::milliseconds{delay > 0 ? delay : 0};
}

/**
 * @brief Wait for one scheduled HTTP result.
 *
 * The slot is detached from the registry before waiting so a late worker
 * completion cannot write into an abandoned entry. The shared slot itself
 * stays alive through the returned handle.
 *
 * @param[in] impl Engine that owns the request.
 * @param[in] id Identifier returned when the request was scheduled.
 * @param[out] out Receives the response when the wait succeeds.
 * @param[in] timeout Maximum time to wait for the worker.
 * @return True when a result was collected, false on timeout or unknown id.
 */
auto wait_http_result(LuaEngine::Impl &impl, uint64_t id, net::HttpResult &out,
                      std::chrono::milliseconds timeout) -> bool {
  std::shared_ptr<LuaEngine::Impl::HttpSlot> slot;
  {
    std::lock_guard<std::mutex> lock(impl.pending_mutex);
    auto it = impl.http_slots.find(id);
    if (it == impl.http_slots.end()) {
      return false;
    }
    slot = it->second;
    impl.http_slots.erase(it);
  }
  std::unique_lock<std::mutex> lock(slot->mutex);
  if (!slot->ready.wait_for(lock, timeout, [&slot] { return slot->done; })) {
    return false;
  }
  out = std::move(slot->result);
  return true;
}

/**
 * @brief Report whether a scheduled HTTP result can be resumed.
 *
 * The caller must hold the engine pending mutex so the slot registry cannot
 * change while the lookup runs.
 *
 * @param[in] impl Engine that owns the request.
 * @param[in] id Identifier returned when the request was scheduled.
 * @return True when the result is ready or the slot is already gone.
 */
auto http_ready_locked(LuaEngine::Impl &impl, uint64_t id) -> bool {
  auto it = impl.http_slots.find(id);
  if (it == impl.http_slots.end()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(it->second->mutex);
  return it->second->done;
}

/** @brief Push one HTTP result as a Lua table. */
auto push_http_result(lua_State *state, const net::HttpResult &result) -> void {
  lua_createtable(state, 0, 5);
  lua_pushboolean(state, result.ok ? 1 : 0);
  lua_setfield(state, -2, "ok");
  lua_pushinteger(state, static_cast<lua_Integer>(result.status));
  lua_setfield(state, -2, "status");
  lua_pushlstring(state, result.body.data(), result.body.size());
  lua_setfield(state, -2, "body");
  lua_pushlstring(state, result.error.data(), result.error.size());
  lua_setfield(state, -2, "error");
  lua_createtable(state, 0, static_cast<int>(result.headers.size()));
  for (size_t i = 0; i < result.headers.size(); ++i) {
    lua_pushinteger(state, static_cast<lua_Integer>(i + 1));
    lua_createtable(state, 0, 2);
    const auto &[name, value] = result.headers[i];
    lua_pushlstring(state, name.data(), name.size());
    lua_setfield(state, -2, "name");
    lua_pushlstring(state, value.data(), value.size());
    lua_setfield(state, -2, "value");
    lua_settable(state, -3);
  }
  lua_setfield(state, -2, "headers");
}

/** @brief Read request settings from an optional Lua table. */
auto parse_http_options(lua_State *state, int index, LuaEngine::Impl &impl,
                        net::HttpRequestOptions &options, int &delay_ms)
    -> void {
  if (!lua_istable(state, index)) {
    return;
  }
  index = lua_absindex(state, index);
  lua_getfield(state, index, "method");
  if (lua_isstring(state, -1)) {
    options.method = lua_tostring(state, -1);
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "body");
  if (lua_isstring(state, -1)) {
    size_t length{0};
    const char *text = lua_tolstring(state, -1, &length);
    options.body.assign(text, length);
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "timeout");
  if (lua_isinteger(state, -1)) {
    const auto value = static_cast<int>(lua_tointeger(state, -1));
    if (value > 0) {
      options.timeout_ms = value;
    }
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "maxBytes");
  if (lua_isinteger(state, -1)) {
    const auto value = static_cast<int64_t>(lua_tointeger(state, -1));
    if (value > 0) {
      options.max_bytes = static_cast<size_t>(value);
    }
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "delay");
  if (lua_isinteger(state, -1)) {
    const auto value = static_cast<int>(lua_tointeger(state, -1));
    if (value > 0) {
      delay_ms = value;
    }
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "socks5");
  if (lua_isboolean(state, -1)) {
    options.use_socks5 = lua_toboolean(state, -1) != 0;
  } else {
    std::lock_guard<std::mutex> lock{impl.http_config_mutex};
    options.use_socks5 = impl.http_socks5_enabled;
  }
  lua_pop(state, 1);
  lua_getfield(state, index, "headers");
  if (lua_istable(state, -1)) {
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
      if (lua_type(state, -2) == LUA_TSTRING && lua_isstring(state, -1)) {
        size_t name_length{0};
        size_t value_length{0};
        const char *name = lua_tolstring(state, -2, &name_length);
        const char *value = lua_tolstring(state, -1, &value_length);
        options.headers.emplace_back(std::string{name, name_length},
                                     std::string{value, value_length});
      }
      lua_pop(state, 1);
    }
  }
  lua_pop(state, 1);
}

/** @brief Run one fetch either cooperatively or as a blocking call. */
auto perform_fetch(lua_State *state, LuaEngine::Impl &impl, std::string url,
                   net::HttpRequestOptions options, int delay_ms) -> int {
  if (options.use_socks5) {
    std::lock_guard<std::mutex> lock{impl.http_config_mutex};
    options.socks5 = impl.http_socks5;
  }
  if (lua_isyieldable(state)) {
    auto slot = std::make_shared<LuaEngine::Impl::HttpSlot>();
    const TaskId id = impl.scheduler->schedule_delayed(
        [slot, url, options]() mutable {
          auto result = net::http_request(url, options);
          {
            std::lock_guard<std::mutex> lock(slot->mutex);
            slot->result = std::move(result);
            slot->done = true;
          }
          slot->ready.notify_all();
        },
        std::chrono::milliseconds{delay_ms}, "http");
    if (id == kInvalidTaskId) {
      spx::log::warningf("script", "http task rejected by scheduler");
      push_http_result(
          state, net::HttpResult{false, 0, {}, {}, "scheduler unavailable"});
      return 1;
    }
    {
      std::lock_guard<std::mutex> lock(impl.pending_mutex);
      impl.http_slots[id] = slot;
    }
    spx::log::infof("script",
                    "http task scheduled id=%llu timeout=%d delay=%d socks5=%d",
                    static_cast<unsigned long long>(id), options.timeout_ms,
                    delay_ms, options.use_socks5 ? 1 : 0);
    lua_createtable(state, 0, 2);
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    lua_setfield(state, -2, "httpId");
    const int wait = options.timeout_ms + delay_ms + 1000;
    lua_pushinteger(state, wait);
    lua_setfield(state, -2, "delay");
    return lua_yield(state, 1);
  }
  if (delay_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
  }
  const auto result = net::http_request(url, options);
  push_http_result(state, result);
  return 1;
}

/** @brief Fetch one URL from Lua. */
auto l_fetch(lua_State *state) -> int {
  auto *impl = engine_of(state);
  if (impl == nullptr || !lua_isstring(state, 1)) {
    lua_pushnil(state);
    return 1;
  }
  size_t length{0};
  const char *raw = lua_tolstring(state, 1, &length);
  const std::string url{raw, length};
  net::HttpRequestOptions options;
  int delay_ms{0};
  parse_http_options(state, 2, *impl, options, delay_ms);
  return perform_fetch(state, *impl, url, options, delay_ms);
}

/** @brief Encode a Lua byte string as Base64. */
auto l_base64_encode(lua_State *state) -> int {
  size_t length{0};
  const char *input = luaL_checklstring(state, 1, &length);
  constexpr std::string_view alphabet{
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
  std::string output;
  output.reserve(((length + 2) / 3) * 4);
  for (size_t index = 0; index < length; index += 3) {
    const auto a = static_cast<uint8_t>(input[index]);
    const auto b = index + 1 < length ? static_cast<uint8_t>(input[index + 1])
                                      : uint8_t{0};
    const auto c = index + 2 < length ? static_cast<uint8_t>(input[index + 2])
                                      : uint8_t{0};
    const uint32_t combined =
        (static_cast<uint32_t>(a) << 16) | (static_cast<uint32_t>(b) << 8) | c;
    output.push_back(alphabet[(combined >> 18) & 0x3F]);
    output.push_back(alphabet[(combined >> 12) & 0x3F]);
    output.push_back(index + 1 < length ? alphabet[(combined >> 6) & 0x3F]
                                        : '=');
    output.push_back(index + 2 < length ? alphabet[combined & 0x3F] : '=');
  }
  lua_pushlstring(state, output.data(), output.size());
  return 1;
}

/** @brief Schedule one Lua callback after a delay. */
auto l_schedule_callback(lua_State *state) -> int {
  auto *impl = engine_of(state);
  if (impl == nullptr || impl->host == nullptr || !lua_isfunction(state, 1)) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const lua_Integer delay = luaL_checkinteger(state, 2);
  lua_pushvalue(state, 1);
  const int callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  auto host = impl->host != nullptr ? impl->host->clone() : nullptr;
  if (!host) {
    luaL_unref(state, LUA_REGISTRYINDEX, callback_ref);
    lua_pushboolean(state, 0);
    return 1;
  }
  const uint32_t session = impl->session;
  const auto due = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds{delay > 0 ? delay : 0};
  const auto delay_ms = static_cast<int>(delay > 0 ? delay : 0);
  {
    std::lock_guard<std::mutex> lock{impl->deferred_mutex};
    impl->deferred_callbacks.push_back(LuaEngine::Impl::DeferredCallback{
        callback_ref, due, std::move(host), session});
  }
  std::string callback_label{"scheduled"};
  lua_Debug debug{};
  if (lua_getstack(state, 1, &debug) != 0 &&
      lua_getinfo(state, "n", &debug) != 0 && debug.name != nullptr) {
    callback_label = debug.name;
  }
  spx::log::infof("script", "callback scheduled name=%s delay=%dms",
                  callback_label.c_str(), delay_ms);
  lua_pushboolean(state, 1);
  return 1;
}

/** @brief Run one request through an HTTP instance. */
auto l_http_call(lua_State *state) -> int {
  auto *impl = engine_of(state);
  if (impl == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  // Accept both obj:get(url) and obj.get(url) call forms.
  const bool has_self = lua_istable(state, 1);
  const int url_index = has_self ? 2 : 1;
  if (!lua_isstring(state, url_index)) {
    lua_pushnil(state);
    return 1;
  }

  net::HttpRequestOptions options;
  int delay_ms{0};
  if (has_self) {
    lua_getfield(state, 1, "options");
    parse_http_options(state, -1, *impl, options, delay_ms);
    lua_pop(state, 1);
  }
  parse_http_options(state, url_index + 1, *impl, options, delay_ms);

  const char *forced = lua_tostring(state, lua_upvalueindex(1));
  if (forced != nullptr && forced[0] != '\0') {
    options.method = forced;
  }
  size_t length{0};
  const char *raw = lua_tolstring(state, url_index, &length);
  return perform_fetch(state, *impl, std::string{raw, length}, options,
                       delay_ms);
}

/** @brief Create one HTTP instance with reusable defaults. */
auto l_http_new(lua_State *state) -> int {
  lua_createtable(state, 0, 5);
  if (lua_istable(state, 1)) {
    lua_pushvalue(state, 1);
  } else {
    lua_newtable(state);
  }
  lua_setfield(state, -2, "options");
  lua_pushlstring(state, "GET", 3);
  lua_pushcclosure(state, l_http_call, 1);
  lua_setfield(state, -2, "get");
  lua_pushlstring(state, "POST", 4);
  lua_pushcclosure(state, l_http_call, 1);
  lua_setfield(state, -2, "post");
  lua_pushlstring(state, "", 0);
  lua_pushcclosure(state, l_http_call, 1);
  lua_setfield(state, -2, "request");
  return 1;
}

/** @brief Toggle SOCKS5 routing for script HTTP requests. */
auto l_set_http_socks5(lua_State *state) -> int {
  auto *impl = engine_of(state);
  bool enabled{false};
  if (impl != nullptr) {
    std::lock_guard<std::mutex> lock{impl->http_config_mutex};
    impl->http_socks5_enabled = lua_toboolean(state, 1) != 0;
    enabled = impl->http_socks5_enabled;
  }
  lua_pushboolean(state, enabled ? 1 : 0);
  return 1;
}

/** @brief Set the SOCKS5 proxy used by script HTTP requests. */
auto l_set_http_socks5_config(lua_State *state) -> int {
  auto *impl = engine_of(state);
  if (impl == nullptr || !lua_isstring(state, 1) || !lua_isinteger(state, 2)) {
    lua_pushboolean(state, 0);
    return 1;
  }
  net::Socks5Config config;
  config.enabled = true;
  config.host = lua_tostring(state, 1);
  const auto port = static_cast<uint32_t>(lua_tointeger(state, 2));
  if (port == 0 || port > 65535) {
    lua_pushboolean(state, 0);
    return 1;
  }
  config.port = static_cast<uint16_t>(port);
  if (lua_isstring(state, 3)) {
    config.username = lua_tostring(state, 3);
  }
  if (lua_isstring(state, 4)) {
    config.password = lua_tostring(state, 4);
  }
  if (!net::socks5_validate(config)) {
    lua_pushboolean(state, 0);
    return 1;
  }
  {
    std::lock_guard<std::mutex> lock{impl->http_config_mutex};
    impl->http_socks5 = config;
    impl->http_socks5_enabled = true;
  }
  lua_pushboolean(state, 1);
  return 1;
}

/** Store one hook callback under a canonical name. */
auto store_hook(lua_State *state, LuaEngine::Impl *engine, int fn_index,
                std::string_view name) -> void {
  if (engine == nullptr || lua_type(state, fn_index) != LUA_TFUNCTION) {
    return;
  }
  const std::string key{canonical_name(name)};
  lua_pushvalue(state, fn_index);
  const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
  engine->hooks[key].push_back(ref);
}

/** Remove one hook callback by canonical name. */
auto remove_hook(lua_State *state, LuaEngine::Impl *engine,
                 std::string_view name) -> void {
  if (engine == nullptr) {
    return;
  }
  const std::string key{canonical_name(name)};
  const auto it = engine->hooks.find(key);
  if (it != engine->hooks.end()) {
    for (const int ref : it->second) {
      luaL_unref(state, LUA_REGISTRYINDEX, ref);
    }
    engine->hooks.erase(it);
  }
}

auto l_log(lua_State *state) -> int {
  spx::log::infof("script", "%s", luaL_checkstring(state, 1));
  return 0;
}

auto l_warn(lua_State *state) -> int {
  spx::log::warningf("script", "%s", luaL_checkstring(state, 1));
  return 0;
}

auto l_version(lua_State *state) -> int {
  auto *engine = engine_of(state);
  lua_pushstring(state, engine == nullptr ? "" : engine->version.c_str());
  return 1;
}

auto l_session_id(lua_State *state) -> int {
  auto *engine = engine_of(state);
  lua_pushinteger(state, engine == nullptr ? 0 : engine->session);
  return 1;
}

auto l_client_count(lua_State *state) -> int {
  auto *engine = engine_of(state);
  const size_t count = engine != nullptr && engine->host != nullptr
                           ? engine->host->client_count()
                           : 0;
  lua_pushinteger(state, static_cast<lua_Integer>(count));
  return 1;
}

auto l_upstream(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushstring(state, "");
    lua_pushinteger(state, 0);
    return 2;
  }
  const auto address = engine->host->server_address();
  lua_pushlstring(state, address.data(), address.size());
  lua_pushinteger(state, engine->host->server_port());
  return 2;
}

auto send_frame(lua_State *state, bool to_server) -> int {
  auto *engine = engine_of(state);
  std::vector<std::byte> frame;
  if (engine == nullptr || engine->host == nullptr ||
      !lua_bytes(state, 1, frame)) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const bool reliable =
      lua_isnoneornil(state, 2) ? true : lua_toboolean(state, 2) != 0;
  const bool ok = to_server ? engine->host->send_to_server(frame, reliable)
                            : engine->host->send_to_client(frame, reliable);
  spx::log::infof("script", "session %u send %s %zu bytes", engine->session,
                  to_server ? "upstream" : "downstream", frame.size());
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_send_to_client(lua_State *state) -> int {
  return send_frame(state, false);
}
auto l_send_to_server(lua_State *state) -> int {
  return send_frame(state, true);
}

auto send_text(lua_State *state, bool to_server) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  size_t length = 0;
  const char *text = luaL_checklstring(state, 2, &length);
  const auto type = static_cast<uint32_t>(luaL_checkinteger(state, 1));
  const auto frame = packet::serialize_payload(
      packet::TextPayload{type, std::string{text, length}});
  const bool ok = frame.empty()
                      ? false
                      : (to_server ? engine->host->send_to_server(frame, true)
                                   : engine->host->send_to_client(frame, true));
  spx::log::infof("script", "session %u send text %s %zu bytes",
                  engine->session, to_server ? "upstream" : "downstream",
                  frame.size());
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_sleep(lua_State *state) -> int {
  const int64_t milliseconds =
      static_cast<int64_t>(luaL_checkinteger(state, 1));
  if (milliseconds <= 0) {
    return 0;
  }
  if (lua_isyieldable(state)) {
    lua_pushinteger(state, static_cast<lua_Integer>(milliseconds));
    return lua_yield(state, 1);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{milliseconds});
  return 0;
}

auto l_send_packet(lua_State *state) -> int { return send_text(state, true); }
auto l_send_packet_to_client(lua_State *state) -> int {
  return send_text(state, false);
}

auto l_send_chat(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  size_t length = 0;
  const char *text = luaL_checklstring(state, 1, &length);
  std::string body{"action|input\n|text|"};
  body.append(text, length);
  const auto frame =
      packet::serialize_payload(packet::TextPayload{2u, std::move(body)});
  const bool ok = !frame.empty() && engine->host->send_to_server(frame, true);
  spx::log::infof("script", "session %u send chat upstream %zu bytes",
                  engine->session, frame.size());
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_send_variant(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  packet::builder::VariantBuilder builder;
  builder.function(luaL_checkstring(state, 1));
  const int top = lua_gettop(state);
  for (int i = 2; i <= top && i <= 8; ++i) {
    const size_t index = static_cast<size_t>(i - 1);
    if (lua_type(state, i) == LUA_TSTRING) {
      builder.text(index, lua_tostring(state, i));
    } else if (lua_isinteger(state, i)) {
      builder.param_i32(index, static_cast<int32_t>(lua_tointeger(state, i)));
    } else if (lua_type(state, i) == LUA_TNUMBER) {
      builder.param_float(index, static_cast<float>(lua_tonumber(state, i)));
    }
  }
  const auto frame = builder.frame();
  const bool ok = !frame.empty() && engine->host->send_to_client(frame, true);
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_send_console(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const auto frame =
      packet::builder::console_message_frame(luaL_checkstring(state, 1));
  lua_pushboolean(state, engine->host->send_to_client(frame, true) ? 1 : 0);
  return 1;
}

auto l_send_log(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const auto frame = packet::builder::log_frame(luaL_checkstring(state, 1));
  lua_pushboolean(state, engine->host->send_to_client(frame, true) ? 1 : 0);
  return 1;
}

auto l_tank_new(lua_State *state) -> int {
  push_tank(state, packet::GameUpdatePacket{});
  return 1;
}

auto l_send_raw(lua_State *state) -> int {
  auto *engine = engine_of(state);
  auto *tank = get_tank(state, 1);
  if (engine == nullptr || engine->host == nullptr || tank == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const auto frame =
      packet::serialize_payload(packet::GamePayload{tank->value, {}});
  const bool ok = !frame.empty() && engine->host->send_to_server(frame, true);
  spx::log::infof("script", "session %u send raw upstream %zu bytes",
                  engine->session, frame.size());
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_send_raw_to_client(lua_State *state) -> int {
  auto *engine = engine_of(state);
  auto *tank = get_tank(state, 1);
  if (engine == nullptr || engine->host == nullptr || tank == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const auto frame =
      packet::serialize_payload(packet::GamePayload{tank->value, {}});
  const bool ok = !frame.empty() && engine->host->send_to_client(frame, true);
  spx::log::infof("script", "session %u send raw downstream %zu bytes",
                  engine->session, frame.size());
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_decode(lua_State *state) -> int {
  auto *engine = engine_of(state);
  std::vector<std::byte> frame;
  if (!lua_bytes(state, 1, frame)) {
    lua_pushnil(state);
    return 1;
  }
  push_packet(state,
              view_from_frame(frame, engine == nullptr ? 0 : engine->session));
  return 1;
}

auto l_add_hook(lua_State *state) -> int {
  const int fn_index = lua_type(state, 1) == LUA_TFUNCTION ? 1 : 2;
  const int name_index = fn_index == 1 ? 2 : 1;
  if (lua_type(state, name_index) != LUA_TSTRING) {
    return 0;
  }
  store_hook(state, engine_of(state), fn_index,
             luaL_checkstring(state, name_index));
  return 0;
}

auto l_remove_hook(lua_State *state) -> int {
  remove_hook(state, engine_of(state), luaL_checkstring(state, 1));
  return 0;
}

auto l_remove_all_hooks(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine != nullptr) {
    for (auto &[name, refs] : engine->hooks) {
      for (const int ref : refs) {
        luaL_unref(state, LUA_REGISTRYINDEX, ref);
      }
    }
    engine->hooks.clear();
  }
  return 0;
}

auto l_add_event(lua_State *state) -> int {
  const auto name = event_name(static_cast<int>(luaL_checkinteger(state, 1)));
  if (!name.empty()) {
    store_hook(state, engine_of(state), 2, name);
  }
  return 0;
}

auto l_remove_event(lua_State *state) -> int {
  const auto name = event_name(static_cast<int>(luaL_checkinteger(state, 1)));
  if (!name.empty()) {
    remove_hook(state, engine_of(state), name);
  }
  return 0;
}

auto l_listen_events(lua_State *state) -> int {
  lua_pushboolean(state, 0);
  return 1;
}

auto l_apply_hook(lua_State *state) -> int {
  lua_pushboolean(state, 1);
  return 1;
}

/** @brief Normalize a chat command name by removing its prefix. */
auto command_key(std::string_view name) -> std::string {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  if (out.starts_with('/')) {
    out.erase(0, 1);
  }
  return out;
}

auto l_register_command(lua_State *state) -> int {
  auto *engine = engine_of(state);
  int fn_index = 0;
  const int top = lua_gettop(state);
  for (int i = 2; i <= top; ++i) {
    if (lua_type(state, i) == LUA_TFUNCTION) {
      fn_index = i;
      break;
    }
  }
  if (engine == nullptr || fn_index == 0) {
    return 0;
  }
  const std::string name{command_key(luaL_checkstring(state, 1))};
  if (name.empty()) {
    return 0;
  }
  lua_pushvalue(state, fn_index);
  const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
  const auto it = engine->commands.find(name);
  if (it != engine->commands.end()) {
    luaL_unref(state, LUA_REGISTRYINDEX, it->second);
  }
  engine->commands[name] = ref;
  return 0;
}

auto l_request_script_reload(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  engine->reload_requested = true;
  lua_pushboolean(state, 1);
  return 1;
}

auto l_block_dialog(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const std::string pattern{luaL_checkstring(state, 1)};
  const int32_t count = static_cast<int32_t>(luaL_optinteger(state, 2, 1));
  spx::log::infof("script", "block dialog pattern='%s' count=%d",
                  pattern.c_str(), count);
  const bool ok = add_dialog_block_to(*engine, pattern, count);
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

auto l_bot_send_packet(lua_State *state) -> int {
  if (lua_type(state, 1) == LUA_TTABLE) {
    lua_remove(state, 1);
  }
  return l_send_packet(state);
}

auto l_bot_send_raw(lua_State *state) -> int {
  if (lua_type(state, 1) == LUA_TTABLE) {
    lua_remove(state, 1);
  }
  return l_send_raw(state);
}

/** Push the Mori style bot facade with colon callable methods. */
auto push_bot_table(lua_State *state) -> void {
  lua_createtable(state, 0, 5);
  lua_pushcfunction(state, l_bot_send_packet);
  lua_setfield(state, -2, "sendPacket");
  lua_pushcfunction(state, l_bot_send_raw);
  lua_setfield(state, -2, "sendRaw");
  lua_pushcfunction(state, l_send_chat);
  lua_setfield(state, -2, "say");
  lua_pushcfunction(state, l_upstream);
  lua_setfield(state, -2, "upstream");
  lua_pushcfunction(state, l_session_id);
  lua_setfield(state, -2, "session_id");
  lua_pushcfunction(state, l_client_count);
  lua_setfield(state, -2, "client_count");
}

auto l_get_bot(lua_State *state) -> int {
  push_bot_table(state);
  return 1;
}

auto l_call_hook(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const std::string key{canonical_name(luaL_checkstring(state, 1))};
  const auto it = engine->hooks.find(key);
  if (it == engine->hooks.end()) {
    lua_pushboolean(state, 0);
    return 1;
  }
  lua_rawgeti(state, LUA_REGISTRYINDEX, it->second.front());
  lua_insert(state, 1);
  lua_remove(state, 2);
  const bool ok =
      protected_call(state, lua_gettop(state) - 1, 0, "CallHookCallback");
  lua_pushboolean(state, ok ? 1 : 0);
  return 1;
}

/** @brief Push one player snapshot as a Lua table. */
auto push_player(lua_State *lua, const spx::state::Player &player) -> void {
  lua_createtable(lua, 0, 7);
  lua_pushinteger(lua, player.net_id);
  lua_setfield(lua, -2, "netID");
  lua_pushinteger(lua, player.user_id);
  lua_setfield(lua, -2, "userID");
  lua_pushstring(lua, player.name.c_str());
  lua_setfield(lua, -2, "name");
  lua_pushstring(lua, player.country.c_str());
  lua_setfield(lua, -2, "country");
  lua_pushnumber(lua, player.x);
  lua_setfield(lua, -2, "posX");
  lua_pushnumber(lua, player.y);
  lua_setfield(lua, -2, "posY");
  lua_pushboolean(lua, player.facing_left ? 1 : 0);
  lua_setfield(lua, -2, "isLeft");
}

auto l_get_player_list(lua_State *lua) -> int {
  const auto players = spx::player_manager().snapshot();
  lua_createtable(lua, 0, static_cast<int>(players.size()));
  for (size_t i = 0; i < players.size(); ++i) {
    push_player(lua, players[i]);
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

auto l_get_player_by_net_id(lua_State *lua) -> int {
  const auto player = spx::player_manager().find(
      static_cast<int32_t>(luaL_checkinteger(lua, 1)));
  if (!player) {
    lua_pushnil(lua);
    return 1;
  }
  push_player(lua, *player);
  return 1;
}

auto l_get_local(lua_State *lua) -> int {
  const auto player = spx::player_manager().first();
  if (!player) {
    lua_pushnil(lua);
    return 1;
  }
  push_player(lua, *player);
  return 1;
}

/** @brief Push one tile snapshot as a Lua table. */
auto push_tile(lua_State *lua, const spx::state::Tile &tile) -> void {
  lua_createtable(lua, 0, 5);
  lua_pushinteger(lua, tile.x);
  lua_setfield(lua, -2, "x");
  lua_pushinteger(lua, tile.y);
  lua_setfield(lua, -2, "y");
  lua_pushinteger(lua, tile.foreground);
  lua_setfield(lua, -2, "fg");
  lua_pushinteger(lua, tile.background);
  lua_setfield(lua, -2, "bg");
  lua_pushinteger(lua, tile.flags);
  lua_setfield(lua, -2, "flags");
  lua_pushstring(lua, spx::item_name(tile.foreground).c_str());
  lua_setfield(lua, -2, "fgName");
  lua_pushstring(lua, spx::item_name(tile.background).c_str());
  lua_setfield(lua, -2, "bgName");
}

auto l_get_tile(lua_State *lua) -> int {
  const auto tile =
      spx::tile_manager().get(static_cast<int32_t>(luaL_checkinteger(lua, 1)),
                              static_cast<int32_t>(luaL_checkinteger(lua, 2)));
  if (!tile) {
    lua_pushnil(lua);
    return 1;
  }
  push_tile(lua, *tile);
  return 1;
}

auto l_get_tiles(lua_State *lua) -> int {
  const auto tiles = spx::tile_manager().snapshot();
  lua_createtable(lua, 0, static_cast<int>(tiles.size()));
  for (size_t i = 0; i < tiles.size(); ++i) {
    push_tile(lua, tiles[i]);
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

auto l_get_object_list(lua_State *lua) -> int {
  const auto objects = spx::object_manager().snapshot();
  lua_createtable(lua, 0, static_cast<int>(objects.size()));
  for (size_t i = 0; i < objects.size(); ++i) {
    const auto &object = objects[i];
    lua_createtable(lua, 0, 5);
    lua_pushinteger(lua, object.object_id);
    lua_setfield(lua, -2, "id");
    lua_pushinteger(lua, object.item_id);
    lua_setfield(lua, -2, "itemid");
    lua_pushnumber(lua, object.x);
    lua_setfield(lua, -2, "x");
    lua_pushnumber(lua, object.y);
    lua_setfield(lua, -2, "y");
    lua_pushinteger(lua, object.amount);
    lua_setfield(lua, -2, "amount");
    lua_pushstring(lua, spx::item_name(object.item_id).c_str());
    lua_setfield(lua, -2, "name");
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

auto l_get_npc_list(lua_State *lua) -> int {
  const auto npcs = spx::npc_manager().snapshot();
  lua_createtable(lua, 0, static_cast<int>(npcs.size()));
  for (size_t i = 0; i < npcs.size(); ++i) {
    const auto &npc = npcs[i];
    lua_createtable(lua, 0, 5);
    lua_pushinteger(lua, npc.net_id);
    lua_setfield(lua, -2, "netID");
    lua_pushinteger(lua, npc.npc_id);
    lua_setfield(lua, -2, "id");
    lua_pushnumber(lua, npc.x);
    lua_setfield(lua, -2, "x");
    lua_pushnumber(lua, npc.y);
    lua_setfield(lua, -2, "y");
    lua_pushinteger(lua, npc.flags);
    lua_setfield(lua, -2, "flags");
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

auto l_get_inventory(lua_State *lua) -> int {
  const auto items = spx::player_manager().inventory();
  lua_createtable(lua, 0, static_cast<int>(items.size()));
  for (size_t i = 0; i < items.size(); ++i) {
    const auto &item = items[i];
    lua_createtable(lua, 0, 3);
    lua_pushinteger(lua, item.id);
    lua_setfield(lua, -2, "id");
    lua_pushinteger(lua, item.count);
    lua_setfield(lua, -2, "count");
    lua_pushinteger(lua, item.flags);
    lua_setfield(lua, -2, "flags");
    lua_pushstring(lua, spx::item_name(item.id).c_str());
    lua_setfield(lua, -2, "name");
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

auto l_get_inventory_slots(lua_State *lua) -> int {
  const auto slots = spx::player_manager().inventory_slots();
  lua_createtable(lua, 0, static_cast<int>(slots.size()));
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto &item = slots[i];
    lua_createtable(lua, 0, 5);
    lua_pushinteger(lua, item.id);
    lua_setfield(lua, -2, "id");
    lua_pushinteger(lua, item.count);
    lua_setfield(lua, -2, "count");
    lua_pushinteger(lua, item.flags);
    lua_setfield(lua, -2, "flags");
    lua_pushstring(lua, spx::item_name(item.id).c_str());
    lua_setfield(lua, -2, "name");
    lua_pushboolean(lua, item.id == 0 ? 1 : 0);
    lua_setfield(lua, -2, "empty");
    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
  }
  return 1;
}

/** @brief Apply one inventory slot change and mirror it to the client. */
auto l_set_inventory_slot(lua_State *state) -> int {
  auto *engine = engine_of(state);
  if (engine == nullptr || engine->host == nullptr) {
    lua_pushboolean(state, 0);
    return 1;
  }
  const int32_t slot = static_cast<int32_t>(luaL_checkinteger(state, 1));
  const uint32_t item_id = static_cast<uint32_t>(luaL_checkinteger(state, 2));
  const int32_t count = static_cast<int32_t>(luaL_checkinteger(state, 3));
  const auto slots = spx::player_manager().inventory_slots();
  if (slot < 1 || static_cast<size_t>(slot) > slots.size() || count < 0 ||
      count > 200) {
    lua_pushboolean(state, 0);
    return 1;
  }

  const auto &old = slots[static_cast<size_t>(slot - 1)];
  int32_t removal = 0;
  int32_t addition = 0;
  if (old.id == item_id && item_id != 0) {
    const int32_t delta = count - static_cast<int32_t>(old.count);
    if (delta > 0) {
      addition = delta;
    } else if (delta < 0) {
      removal = -delta;
    }
  } else {
    if (old.id != 0) {
      removal = static_cast<int32_t>(old.count);
    }
    if (item_id != 0) {
      addition = count;
    }
  }

  bool sent = true;
  if (removal > 0) {
    auto frame = packet::builder::GameBuilder{}
                     .type(packet::PacketType::ModifyItemInventory)
                     .net_id(0xFFFFFFFFu)
                     .int_data(static_cast<int32_t>(old.id))
                     .build();
    frame.packet.pad0_[1] = static_cast<uint8_t>(removal);
    const auto bytes = packet::serialize_payload(frame);
    sent = !bytes.empty() && engine->host->send_to_client(bytes, true);
  }
  if (sent && addition > 0) {
    auto frame = packet::builder::GameBuilder{}
                     .type(packet::PacketType::ModifyItemInventory)
                     .net_id(0xFFFFFFFFu)
                     .int_data(static_cast<int32_t>(item_id))
                     .build();
    frame.packet.pad0_[2] = static_cast<uint8_t>(addition);
    const auto bytes = packet::serialize_payload(frame);
    sent = !bytes.empty() && engine->host->send_to_client(bytes, true);
  }
  if (sent) {
    auto &manager = spx::player_manager();
    if (removal > 0) {
      manager.modify_inventory(static_cast<int32_t>(old.id), -removal);
    }
    if (addition > 0) {
      manager.modify_inventory(static_cast<int32_t>(item_id), addition);
    }
  }
  spx::log::infof("script", "session %u set inventory slot %d to %u x%d",
                  engine->session, slot, item_id, count);
  lua_pushboolean(state, sent ? 1 : 0);
  return 1;
}

auto l_get_item_name(lua_State *lua) -> int {
  const auto item = spx::item_database().find(
      static_cast<uint32_t>(luaL_checkinteger(lua, 1)));
  if (!item) {
    lua_pushnil(lua);
    return 1;
  }
  lua_pushstring(lua, item->name.c_str());
  return 1;
}

auto l_get_item_info(lua_State *lua) -> int {
  const auto item = spx::item_database().find(
      static_cast<uint32_t>(luaL_checkinteger(lua, 1)));
  if (!item) {
    lua_pushnil(lua);
    return 1;
  }
  lua_createtable(lua, 0, 8);
  lua_pushinteger(lua, item->flags);
  lua_setfield(lua, -2, "flags");
  lua_pushboolean(lua, (item->flags & 0x8000u) != 0 ? 1 : 0);
  lua_setfield(lua, -2, "untradeable");
  lua_pushinteger(lua, item->id);
  lua_setfield(lua, -2, "id");
  lua_pushstring(lua, item->name.c_str());
  lua_setfield(lua, -2, "name");
  lua_pushinteger(lua, item->type);
  lua_setfield(lua, -2, "type");
  lua_pushinteger(lua, item->material);
  lua_setfield(lua, -2, "material");
  lua_pushinteger(lua, item->rarity);
  lua_setfield(lua, -2, "rarity");
  lua_pushinteger(lua, item->max_amount);
  lua_setfield(lua, -2, "maxAmount");
  return 1;
}

auto l_get_items_dat(lua_State *lua) -> int {
  const auto data = spx::item_data();
  if (!data.valid) {
    lua_pushnil(lua);
    lua_pushstring(lua, "items.dat not received");
    return 2;
  }
  lua_pushlstring(lua, reinterpret_cast<const char *>(data.bytes.data()),
                  data.bytes.size());
  lua_pushnil(lua);
  return 2;
}

auto l_get_device_info(lua_State *lua) -> int {
  const auto info = platform::device_info();
  lua_createtable(lua, 0, 5);
  lua_pushstring(lua, info.hostname.c_str());
  lua_setfield(lua, -2, "hostname");
  lua_pushstring(lua, info.os.c_str());
  lua_setfield(lua, -2, "os");
  lua_pushstring(lua, info.platform.c_str());
  lua_setfield(lua, -2, "platform");
  lua_pushstring(lua, info.arch.c_str());
  lua_setfield(lua, -2, "arch");
  lua_pushinteger(lua, static_cast<lua_Integer>(info.cpu_count));
  lua_setfield(lua, -2, "cpuCount");
  return 1;
}

auto l_get_world_name(lua_State *lua) -> int {
  const auto info = spx::tile_manager().info();
  lua_pushstring(lua, info.name.c_str());
  return 1;
}

auto l_get_world_tile_map(lua_State *lua) -> int {
  const auto info = spx::tile_manager().info();
  lua_createtable(lua, 0, 4);
  lua_pushstring(lua, info.name.c_str());
  lua_setfield(lua, -2, "name");
  lua_pushinteger(lua, info.width);
  lua_setfield(lua, -2, "width");
  lua_pushinteger(lua, info.height);
  lua_setfield(lua, -2, "height");
  lua_pushinteger(lua, info.tile_count);
  lua_setfield(lua, -2, "count");
  return 1;
}

auto register_api(lua_State *state, LuaEngine::Impl *engine) -> void {
  lua_pushlightuserdata(state, engine);
  lua_setfield(state, LUA_REGISTRYINDEX, kEngineKey);

  lua_pushcfunction(state, l_log);
  lua_setglobal(state, "log");
  lua_pushcfunction(state, l_warn);
  lua_setglobal(state, "warn");
  lua_pushcfunction(state, l_version);
  lua_setglobal(state, "version");
  lua_pushcfunction(state, l_session_id);
  lua_setglobal(state, "session_id");
  lua_pushcfunction(state, l_client_count);
  lua_setglobal(state, "client_count");
  lua_pushcfunction(state, l_upstream);
  lua_setglobal(state, "upstream");

  lua_pushcfunction(state, l_send_to_client);
  lua_setglobal(state, "send_to_client");
  lua_pushcfunction(state, l_send_to_server);
  lua_setglobal(state, "send_to_server");
  lua_pushcfunction(state, l_send_packet);
  lua_setglobal(state, "sendPacket");
  lua_pushcfunction(state, l_sleep);
  lua_setglobal(state, "sleep");
  lua_pushcfunction(state, l_sleep);
  lua_setglobal(state, "sleepMs");
  lua_pushcfunction(state, l_send_packet_to_client);
  lua_setglobal(state, "sendPacketToClient");
  lua_pushcfunction(state, l_send_chat);
  lua_setglobal(state, "send_chat");
  lua_pushcfunction(state, l_send_chat);
  lua_setglobal(state, "chat");
  lua_pushcfunction(state, l_send_variant);
  lua_setglobal(state, "sendVariant");
  lua_pushcfunction(state, l_send_console);
  lua_setglobal(state, "send_console");
  lua_pushcfunction(state, l_send_log);
  lua_setglobal(state, "send_log");
  lua_pushcfunction(state, l_send_raw);
  lua_setglobal(state, "sendRaw");
  lua_pushcfunction(state, l_send_raw);
  lua_setglobal(state, "sendPacketRaw");
  lua_pushcfunction(state, l_send_raw_to_client);
  lua_setglobal(state, "sendRawToClient");

  lua_pushcfunction(state, l_add_hook);
  lua_setglobal(state, "addHook");
  lua_pushcfunction(state, l_add_hook);
  lua_setglobal(state, "AddHookCallback");
  lua_pushcfunction(state, l_remove_hook);
  lua_setglobal(state, "removeHook");
  lua_pushcfunction(state, l_remove_hook);
  lua_setglobal(state, "RemoveHookCallback");
  lua_pushcfunction(state, l_remove_all_hooks);
  lua_setglobal(state, "removeAllHooks");
  lua_pushcfunction(state, l_remove_all_hooks);
  lua_setglobal(state, "removeEvents");
  lua_pushcfunction(state, l_call_hook);
  lua_setglobal(state, "CallHookCallback");
  lua_pushcfunction(state, l_add_event);
  lua_setglobal(state, "addEvent");
  lua_pushcfunction(state, l_remove_event);
  lua_setglobal(state, "removeEvent");
  lua_pushcfunction(state, l_listen_events);
  lua_setglobal(state, "listenEvents");
  lua_pushcfunction(state, l_listen_events);
  lua_setglobal(state, "unlistenEvents");
  lua_pushcfunction(state, l_apply_hook);
  lua_setglobal(state, "applyHook");
  lua_pushcfunction(state, l_register_command);
  lua_setglobal(state, "registerCommand");
  lua_pushcfunction(state, l_register_command);
  lua_setglobal(state, "addCommand");
  lua_pushcfunction(state, l_request_script_reload);
  lua_setglobal(state, "requestScriptReload");
  lua_pushcfunction(state, l_block_dialog);
  lua_setglobal(state, "blockDialog");

  lua_createtable(state, 0, static_cast<int>(kEventNames.size()));
  for (size_t i = 0; i < kEventNames.size(); ++i) {
    lua_pushinteger(state, static_cast<lua_Integer>(i + 1));
    lua_setfield(state, -2, std::string{kEventNames[i]}.c_str());
  }
  lua_setglobal(state, "Event");

  lua_createtable(state, 0, 1);
  lua_pushcfunction(state, l_tank_new);
  lua_setfield(state, -2, "new");
  lua_setglobal(state, "GameUpdatePacket");

  lua_pushcfunction(state, l_decode);
  lua_setglobal(state, "decode");
  lua_createtable(state, 0, 1);
  lua_pushcfunction(state, l_decode);
  lua_setfield(state, -2, "decode");
  lua_setglobal(state, "Packet");

  lua_pushcfunction(state, l_get_bot);
  lua_setglobal(state, "getBot");

  lua_pushcfunction(state, l_get_player_list);
  lua_setglobal(state, "getPlayerList");
  lua_pushcfunction(state, l_get_player_by_net_id);
  lua_setglobal(state, "getPlayerByNetID");
  lua_pushcfunction(state, l_get_local);
  lua_setglobal(state, "getLocal");
  lua_pushcfunction(state, l_get_tile);
  lua_setglobal(state, "getTile");
  lua_pushcfunction(state, l_get_tiles);
  lua_setglobal(state, "getTiles");
  lua_pushcfunction(state, l_get_object_list);
  lua_setglobal(state, "getObjectList");
  lua_pushcfunction(state, l_get_npc_list);
  lua_setglobal(state, "getNPCList");
  lua_pushcfunction(state, l_get_inventory);
  lua_setglobal(state, "getInventory");
  lua_pushcfunction(state, l_get_inventory_slots);
  lua_setglobal(state, "getInventorySlots");
  lua_pushcfunction(state, l_set_inventory_slot);
  lua_setglobal(state, "setInventorySlot");
  lua_pushcfunction(state, l_get_item_name);
  lua_setglobal(state, "getItemName");
  lua_pushcfunction(state, l_get_item_info);
  lua_setglobal(state, "getItemInfo");
  lua_pushcfunction(state, l_get_items_dat);
  lua_setglobal(state, "getItemsDat");
  lua_pushcfunction(state, l_get_world_name);
  lua_setglobal(state, "getWorldName");
  lua_pushcfunction(state, l_get_world_name);
  lua_setglobal(state, "getCurrentWorldName");
  lua_pushcfunction(state, l_get_world_tile_map);
  lua_setglobal(state, "getWorldTileMap");
  lua_pushcfunction(state, l_get_device_info);
  lua_setglobal(state, "getDeviceInfo");
  lua_pushcfunction(state, l_fetch);
  lua_setglobal(state, "fetch");
  lua_pushcfunction(state, l_base64_encode);
  lua_setglobal(state, "base64Encode");
  lua_pushcfunction(state, l_schedule_callback);
  lua_setglobal(state, "scheduleCallback");
  lua_createtable(state, 0, 1);
  lua_pushcfunction(state, l_http_new);
  lua_setfield(state, -2, "new");
  lua_setglobal(state, "Http");
  lua_pushcfunction(state, l_set_http_socks5);
  lua_setglobal(state, "setHttpSocks5");
  lua_pushcfunction(state, l_set_http_socks5_config);
  lua_setglobal(state, "setHttpSocks5Config");
}

} // namespace

LuaEngine::LuaEngine() : impl_{std::make_unique<Impl>()} {
  impl_->state = luaL_newstate();
  if (impl_->state == nullptr) {
    return;
  }
  luaL_openlibs(impl_->state);
  impl_->scheduler = std::make_unique<Scheduler>();
  register_api(impl_->state, impl_.get());
}

LuaEngine::~LuaEngine() {
  if (impl_->scheduler != nullptr) {
    impl_->scheduler->stop();
    impl_->scheduler.reset();
  }
  if (impl_->state != nullptr) {
    lua_close(impl_->state);
    impl_->state = nullptr;
  }
}

auto LuaEngine::add_dialog_block(std::string_view pattern, int32_t count)
    -> bool {
  return impl_ != nullptr && add_dialog_block_to(*impl_, pattern, count);
}

auto LuaEngine::consume_dialog_block(std::string_view dialog) -> bool {
  return impl_ != nullptr && consume_dialog_block_from(*impl_, dialog);
}

void LuaEngine::set_version(std::string_view value) {
  impl_->version.assign(value);
}

void LuaEngine::inherit_socks5(const net::Socks5Config &config) {
  if (impl_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock{impl_->http_config_mutex};
  impl_->http_socks5 = config;
  impl_->http_socks5_enabled = config.enabled && net::socks5_validate(config);
}

auto LuaEngine::command_names() const -> std::vector<std::string> {
  std::vector<std::string> result;
  result.reserve(impl_->commands.size());
  for (const auto &[name, ref] : impl_->commands) {
    (void)ref;
    result.push_back(name);
  }
  return result;
}

auto LuaEngine::take_reload_request() noexcept -> bool {
  if (impl_ == nullptr || !impl_->reload_requested) {
    return false;
  }
  impl_->reload_requested = false;
  return true;
}

auto LuaEngine::invoke_command(std::string_view name,
                               std::span<const std::string> args,
                               ScriptHost &host) -> bool {
  if (impl_->state == nullptr) {
    return false;
  }
  const std::string key{command_key(name)};
  const auto it = impl_->commands.find(key);
  if (it == impl_->commands.end()) {
    return false;
  }

  lua_State *main = impl_->state;
  lua_State *coroutine = lua_newthread(main);
  const int ref = luaL_ref(main, LUA_REGISTRYINDEX);
  lua_rawgeti(main, LUA_REGISTRYINDEX, it->second);
  lua_xmove(main, coroutine, 1);
  for (const auto &arg : args) {
    lua_pushlstring(main, arg.data(), arg.size());
    lua_xmove(main, coroutine, 1);
  }

  const uint32_t session = host.session_id();
  impl_->host = &host;
  impl_->session = session;
  int status =
      run_coroutine(coroutine, static_cast<int>(args.size()), "chat command");
  bool ok = status == LUA_OK;

  if (status == LUA_YIELD) {
    Impl::PendingCommand task;
    task.coroutine_ref = ref;
    task.session = session;
    read_defer(coroutine, task);
    auto owned = host.clone();
    if (owned) {
      task.host = std::move(owned);
      std::lock_guard<std::mutex> lock{impl_->pending_mutex};
      impl_->pending.push_back(std::move(task));
      ok = true;
    } else {
      while (status == LUA_YIELD) {
        if (task.http_id != 0) {
          net::HttpResult result;
          const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  task.due - std::chrono::steady_clock::now());
          if (!wait_http_result(*impl_, task.http_id, result, remaining)) {
            result.error = "timeout";
          }
          spx::log::infof("script",
                          "http task blocking id=%llu ok=%d status=%d",
                          static_cast<unsigned long long>(task.http_id),
                          result.ok ? 1 : 0, result.status);
          push_http_result(coroutine, result);
          task.http_id = 0;
          impl_->host = &host;
          impl_->session = session;
          status = run_coroutine(coroutine, 1, "chat command");
        } else {
          const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  task.due - std::chrono::steady_clock::now())
                  .count();
          if (remaining > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{remaining});
          }
          impl_->host = &host;
          impl_->session = session;
          status = run_coroutine(coroutine, 0, "chat command");
        }
        if (status == LUA_YIELD) {
          read_defer(coroutine, task);
        }
      }
      ok = status == LUA_OK;
    }
  }

  lua_settop(coroutine, 0);
  if (ok && status == LUA_OK) {
    luaL_unref(main, LUA_REGISTRYINDEX, ref);
  } else if (!ok) {
    luaL_unref(main, LUA_REGISTRYINDEX, ref);
  }
  impl_->host = nullptr;
  impl_->session = 0;
  return ok;
}

auto LuaEngine::available() const noexcept -> bool {
  return impl_->state != nullptr;
}

auto LuaEngine::poll_callback(uint32_t timeout_ms) -> uint32_t {
  if (impl_->state == nullptr) {
    return timeout_ms;
  }
  const auto now = std::chrono::steady_clock::now();
  std::vector<Impl::PendingCommand> ready;
  std::vector<Impl::DeferredCallback> callbacks;
  {
    std::lock_guard<std::mutex> lock{impl_->deferred_mutex};
    for (auto it = impl_->deferred_callbacks.begin();
         it != impl_->deferred_callbacks.end();) {
      if (it->due <= now) {
        callbacks.push_back(std::move(*it));
        it = impl_->deferred_callbacks.erase(it);
      } else {
        ++it;
      }
    }
  }
  lua_State *main = impl_->state;
  std::vector<Impl::PendingCommand> callback_tasks;
  uint32_t wait = timeout_ms;
  for (auto &callback : callbacks) {
    lua_State *coroutine = lua_newthread(main);
    const int ref = luaL_ref(main, LUA_REGISTRYINDEX);
    lua_rawgeti(main, LUA_REGISTRYINDEX, callback.callback_ref);
    lua_xmove(main, coroutine, 1);
    luaL_unref(main, LUA_REGISTRYINDEX, callback.callback_ref);
    impl_->host = callback.host.get();
    impl_->session = callback.session;
    const int status = run_coroutine(coroutine, 0, "scheduled callback");
    if (status == LUA_YIELD) {
      Impl::PendingCommand task;
      task.coroutine_ref = ref;
      task.session = callback.session;
      task.host = std::move(callback.host);
      read_defer(coroutine, task);
      callback_tasks.push_back(std::move(task));
    } else {
      lua_settop(coroutine, 0);
      luaL_unref(main, LUA_REGISTRYINDEX, ref);
    }
    impl_->host = nullptr;
    impl_->session = 0;
  }
  if (!callbacks.empty()) {
    wait = 0;
  }
  if (!callback_tasks.empty()) {
    std::lock_guard<std::mutex> lock{impl_->pending_mutex};
    for (auto &task : callback_tasks) {
      impl_->pending.push_back(std::move(task));
    }
  }
  {
    std::lock_guard<std::mutex> lock{impl_->pending_mutex};
    for (auto it = impl_->pending.begin(); it != impl_->pending.end();) {
      if (it->due <= now ||
          (it->http_id != 0 && http_ready_locked(*impl_, it->http_id))) {
        ready.push_back(std::move(*it));
        it = impl_->pending.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (auto &task : ready) {
    lua_rawgeti(main, LUA_REGISTRYINDEX, task.coroutine_ref);
    lua_State *coroutine = lua_tothread(main, -1);
    lua_pop(main, 1);
    if (coroutine == nullptr) {
      luaL_unref(main, LUA_REGISTRYINDEX, task.coroutine_ref);
      continue;
    }
    impl_->host = task.host.get();
    impl_->session = task.session;
    int args{0};
    if (task.http_id != 0) {
      net::HttpResult result;
      if (!wait_http_result(*impl_, task.http_id, result,
                            std::chrono::milliseconds{0})) {
        result.error = "timeout";
      }
      spx::log::infof("script", "http task resumed id=%llu ok=%d status=%d",
                      static_cast<unsigned long long>(task.http_id),
                      result.ok ? 1 : 0, result.status);
      task.http_id = 0;
      push_http_result(coroutine, result);
      args = 1;
    }
    int status = run_coroutine(coroutine, args, "deferred command");
    if (status == LUA_OK) {
      lua_settop(coroutine, 0);
      luaL_unref(main, LUA_REGISTRYINDEX, task.coroutine_ref);
    } else if (status == LUA_YIELD) {
      read_defer(coroutine, task);
      std::lock_guard<std::mutex> lock{impl_->pending_mutex};
      impl_->pending.push_back(std::move(task));
    } else {
      lua_settop(coroutine, 0);
      luaL_unref(main, LUA_REGISTRYINDEX, task.coroutine_ref);
    }
  }
  impl_->host = nullptr;
  impl_->session = 0;

  {
    std::lock_guard<std::mutex> deferred_lock{impl_->deferred_mutex};
    for (const auto &callback : impl_->deferred_callbacks) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(callback.due -
                                                                now)
              .count();
      const uint32_t next =
          remaining > 0 ? static_cast<uint32_t>(remaining) : 0u;
      if (next < wait) {
        wait = next;
      }
    }
  }
  std::lock_guard<std::mutex> lock{impl_->pending_mutex};
  for (const auto &task : impl_->pending) {
    if (task.http_id != 0) {
      wait = (std::min)(wait, 10u);
      continue;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(task.due - now)
            .count();
    const uint32_t next = remaining > 0 ? static_cast<uint32_t>(remaining) : 0u;
    if (next < wait) {
      wait = next;
    }
  }
  return wait;
}

auto LuaEngine::load(std::string_view source, std::string_view name) -> bool {
  if (impl_->state == nullptr) {
    return false;
  }
  g_instruction_budget = kInstructionLimit;
  lua_sethook(impl_->state, instruction_hook, LUA_MASKCOUNT, 1000);
  const int status =
      luaL_loadbufferx(impl_->state, source.data(), source.size(),
                       std::string{name}.c_str(), nullptr);
  if (status == LUA_OK) {
    const int call_status = lua_pcall(impl_->state, 0, 0, 0);
    lua_sethook(impl_->state, nullptr, 0, 0);
    if (call_status != LUA_OK) {
      const char *message = lua_tostring(impl_->state, -1);
      spx::log::warningf("script", "script %.*s failed - %s",
                         static_cast<int>(name.size()), name.data(),
                         message != nullptr ? message : "unknown error");
      lua_pop(impl_->state, 1);
      return false;
    }
    return true;
  }
  const char *message = lua_tostring(impl_->state, -1);
  spx::log::warningf("script", "script %.*s load failed - %s",
                     static_cast<int>(name.size()), name.data(),
                     message != nullptr ? message : "unknown error");
  lua_pop(impl_->state, 1);
  lua_sethook(impl_->state, nullptr, 0, 0);
  return false;
}

auto LuaEngine::on_packet(std::string_view event, const LuaPacketView &view,
                          ScriptHost &host) -> HookReply {
  HookReply reply;
  if (impl_->state == nullptr) {
    return reply;
  }
  const std::string name{canonical_name(event)};
  const auto it = impl_->hooks.find(name);
  if (it == impl_->hooks.end()) {
    return reply;
  }

  impl_->host = &host;
  impl_->session = view.session_id;
  for (const int ref : it->second) {
    lua_rawgeti(impl_->state, LUA_REGISTRYINDEX, ref);
    push_packet(impl_->state, view);
    lua_pushlstring(impl_->state,
                    reinterpret_cast<const char *>(view.frame.data()),
                    view.frame.size());
    HookReply single;
    if (protected_call(impl_->state, 2, 1, "packet hook")) {
      read_decision(impl_->state, single);
      if (single.decision != HookDecision::Forward) {
        reply = single;
      }
    }
    lua_settop(impl_->state, 0);
  }
  impl_->host = nullptr;
  impl_->session = 0;
  return reply;
}

void LuaEngine::on_simple(std::string_view event, uint32_t session_id,
                          std::string_view arg1, std::string_view arg2,
                          ScriptHost &host) {
  if (impl_->state == nullptr) {
    return;
  }
  const std::string name{canonical_name(event)};
  const auto it = impl_->hooks.find(name);
  if (it == impl_->hooks.end()) {
    return;
  }

  impl_->host = &host;
  impl_->session = session_id;
  for (const int ref : it->second) {
    lua_rawgeti(impl_->state, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(impl_->state, static_cast<lua_Integer>(session_id));
    lua_pushlstring(impl_->state, arg1.data(), arg1.size());
    lua_pushlstring(impl_->state, arg2.data(), arg2.size());
    if (!protected_call(impl_->state, 3, 0, "event hook")) {
      lua_settop(impl_->state, 0);
    }
  }
  impl_->host = nullptr;
  impl_->session = 0;
}

void LuaEngine::on_inventory(uint32_t session_id,
                             std::span<const InventoryChange> changes,
                             ScriptHost &host) {
  if (impl_->state == nullptr) {
    return;
  }
  const auto it = impl_->hooks.find("inventory");
  if (it == impl_->hooks.end()) {
    return;
  }

  impl_->host = &host;
  impl_->session = session_id;
  lua_State *lua = impl_->state;
  for (const int ref : it->second) {
    lua_rawgeti(lua, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(lua, static_cast<lua_Integer>(session_id));
    lua_createtable(lua, 0, static_cast<int>(changes.size()));
    for (size_t i = 0; i < changes.size(); ++i) {
      const auto &change = changes[i];
      lua_createtable(lua, 0, 8);
      lua_pushinteger(lua, static_cast<lua_Integer>(change.slot));
      lua_setfield(lua, -2, "slot");
      lua_pushinteger(lua, static_cast<lua_Integer>(change.old_id));
      lua_setfield(lua, -2, "oldId");
      lua_pushinteger(lua, static_cast<lua_Integer>(change.old_count));
      lua_setfield(lua, -2, "oldCount");
      lua_pushinteger(lua, static_cast<lua_Integer>(change.new_id));
      lua_setfield(lua, -2, "newId");
      lua_pushinteger(lua, static_cast<lua_Integer>(change.new_count));
      lua_setfield(lua, -2, "newCount");
      lua_pushstring(lua, spx::item_name(change.new_id).c_str());
      lua_setfield(lua, -2, "name");
      lua_pushstring(lua, spx::item_name(change.old_id).c_str());
      lua_setfield(lua, -2, "oldName");
      lua_pushboolean(lua, change.new_id == 0 ? 1 : 0);
      lua_setfield(lua, -2, "empty");
      lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
    }
    if (!protected_call(lua, 2, 0, "inventory event")) {
      lua_settop(lua, 0);
    }
  }
  impl_->host = nullptr;
  impl_->session = 0;
}

} // namespace spx::script
