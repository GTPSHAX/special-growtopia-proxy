#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../src/script/LuaEngine.hpp"
#include "../src/script/LuaPacket.hpp"
#include "../src/script/Scheduler.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/platforms/Platform.hpp"
#include "specialproxy/script/Script.hpp"
#include "specialproxy/state/GameState.hpp"

using namespace spx;

namespace {

class RecordingHost final : public script::ScriptHost {
public:
  auto send_to_client(std::span<const std::byte> frame, bool) -> bool override {
    client.emplace_back(frame.begin(), frame.end());
    return true;
  }

  auto send_to_server(std::span<const std::byte> frame, bool) -> bool override {
    server.emplace_back(frame.begin(), frame.end());
    return true;
  }

  [[nodiscard]] auto session_id() const -> uint32_t override { return 7; }
  [[nodiscard]] auto server_address() const -> std::string_view override {
    return "1.2.3.4";
  }
  [[nodiscard]] auto server_port() const -> uint16_t override { return 17044; }
  [[nodiscard]] auto client_count() const -> size_t override { return 2; }

  std::vector<std::vector<std::byte>> client;
  std::vector<std::vector<std::byte>> server;
};

struct SharedRecord {
  std::vector<std::vector<std::byte>> client;
  std::vector<std::vector<std::byte>> server;
};

class DeferredHost final : public script::ScriptHost {
public:
  explicit DeferredHost(std::shared_ptr<SharedRecord> record)
      : record_{std::move(record)} {}

  auto send_to_client(std::span<const std::byte> frame, bool) -> bool override {
    record_->client.emplace_back(frame.begin(), frame.end());
    return true;
  }

  auto send_to_server(std::span<const std::byte> frame, bool) -> bool override {
    record_->server.emplace_back(frame.begin(), frame.end());
    return true;
  }

  [[nodiscard]] auto session_id() const -> uint32_t override { return 9; }
  [[nodiscard]] auto server_address() const -> std::string_view override {
    return "1.2.3.4";
  }
  [[nodiscard]] auto server_port() const -> uint16_t override { return 17044; }
  [[nodiscard]] auto client_count() const -> size_t override { return 1; }

  auto clone() -> std::shared_ptr<script::ScriptHost> override {
    return std::make_shared<DeferredHost>(record_);
  }

private:
  std::shared_ptr<SharedRecord> record_;
};

auto text_frame(std::string_view body) -> std::vector<std::byte> {
  return packet::serialize_payload(packet::TextPayload{3u, std::string{body}});
}

auto first_message(std::vector<std::byte> frame) -> std::string {
  auto payload = packet::PacketDecoder::parse_payload(frame);
  if (!payload) {
    return {};
  }
  if (const auto *text =
          packet::get_payload_if<packet::TextPayload>(*payload)) {
    return text->data;
  }
  if (const auto *variant =
          packet::get_payload_if<packet::VariantPayload>(*payload)) {
    const auto *message = variant->list.get<std::string>(1);
    return message == nullptr ? std::string{} : *message;
  }
  return {};
}

auto test_growlauncher_hooks() -> void {
  script::LuaEngine engine;
  assert(engine.available());
  assert(engine.load(R"(
    addHook(function(pkt, raw)
      if pkt.kind == "text" and pkt.body == "drop me" then
        return false
      end
      return true
    end, "onclientpacket")
  )",
                     "drop_hook"));

  RecordingHost host;
  const auto frame = text_frame("drop me");
  const auto view = script::view_from_frame(frame, 7);
  assert(view.kind == "text");
  assert(view.body == "drop me");

  const auto dropped = engine.on_packet("onclientpacket", view, host);
  assert(dropped.decision == script::HookDecision::Drop);

  const auto other_frame = text_frame("keep me");
  const auto other = script::view_from_frame(other_frame, 7);
  const auto forwarded = engine.on_packet("onclientpacket", other, host);
  assert(forwarded.decision == script::HookDecision::Forward);
}

auto test_replacement_hook() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    addHook(function(pkt, raw)
      if pkt.kind == "text" then return "replacement frame" end
      return nil
    end, "onserverpacket")
  )",
                     "replace_hook"));

  RecordingHost host;
  const auto frame = text_frame("original frame");
  const auto view = script::view_from_frame(frame, 7);
  const auto reply = engine.on_packet("onserverpacket", view, host);
  assert(reply.decision == script::HookDecision::Replaced);
  const std::string replaced{reinterpret_cast<const char *>(reply.frame.data()),
                             reply.frame.size()};
  assert(replaced == "replacement frame");
}

auto test_mori_hooks_and_sends() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    addEvent(Event.connect, function(session, address, port)
      send_console("connected")
      sendPacket(3, "action|log\nmsg|from lua")
      getBot():sendPacket(3, "action|log\nmsg|from bot")
      local pkt = GameUpdatePacket.new()
      pkt.type = 18
      pkt.net_id = 9
      pkt.int_x = 5
      sendRaw(pkt)
    end)
    addEvent(Event.gamepacket, function(pkt, raw)
      if pkt.tank.int_x == 5 then send_console("tank seen") end
      local copy = GameUpdatePacket.new()
      copy.type = pkt.tank.type
      copy.int_x = pkt.tank.int_x
      sendRaw(copy)
    end)
  )",
                     "mori_hooks"));

  RecordingHost host;
  engine.on_simple("connect", 7, "1.2.3.4", "17044", host);
  assert(host.client.size() == 1);
  assert(first_message(host.client.front()) == "connected");
  assert(host.server.size() == 3);

  auto payload = packet::PacketDecoder::parse_payload(host.server.back());
  assert(payload.has_value());
  const auto *game = packet::get_payload_if<packet::GamePayload>(*payload);
  assert(game != nullptr);
  assert(game->packet.int_x == 5);
  assert(game->packet.net_id == 9);

  const auto tank_frame = packet::serialize_payload(
      packet::GamePayload{packet::GameUpdatePacket{}, {}});
  auto tank_view = script::view_from_frame(tank_frame, 7);
  auto tank = tank_view.tank;
  tank.int_x = 5;
  host.server.clear();
  const auto view = script::view_from_frame(
      packet::serialize_payload(packet::GamePayload{tank, {}}), 7);
  const auto reply = engine.on_packet("ongamepacket", view, host);
  assert(reply.decision == script::HookDecision::Forward);
  assert(host.client.size() == 2);
  assert(first_message(host.client.back()) == "tank seen");
  assert(host.server.size() == 1);
}

auto test_decode_and_variant() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    addHook(function(pkt, raw)
      local decoded = decode(raw)
      if decoded.kind == "text" then
        send_console("decoded:" .. decoded.body)
      end
    end, "onclientpacket")
    addHook(function(pkt, raw)
      if pkt.name == "OnConsoleMessage" then
        send_console("param:" .. tostring(pkt.params[1]))
      end
    end, "onvariant")
  )",
                     "decode_variant"));

  RecordingHost host;
  const auto frame = text_frame("hi lua");
  engine.on_packet("onclientpacket", script::view_from_frame(frame, 7), host);
  assert(host.client.size() == 1);
  assert(first_message(host.client.front()) == "decoded:hi lua");

  auto variant = packet::builder::VariantBuilder{};
  variant.function("OnConsoleMessage").text(1, "variant text");
  const auto variant_frame = variant.frame();
  engine.on_packet("onvariant", script::view_from_frame(variant_frame, 7),
                   host);
  assert(host.client.size() == 2);
  assert(first_message(host.client.back()) == "param:variant text");
}

auto test_variant_types() -> void {
  script::LuaEngine engine;
  assert(engine.available());
  assert(engine.load(R"(
    addHook(function(packet)
      if packet.kind ~= "variant" then
        return
      end
      send_console("name:" .. tostring(packet.name))
      for _, entry in ipairs(packet.values) do
        local value = entry.value
        if entry.type == "vec2" then
          value = string.format("%.1f,%.1f", value.x, value.y)
        elseif entry.type == "vec3" then
          value = string.format("%.1f,%.1f,%.1f", value.x, value.y, value.z)
        end
        send_console(string.format("%d:%s=%s", entry.index, entry.type,
                                   tostring(value)))
      end
    end, "onclientpacket")
  )",
                     "variant_types"));

  auto variant = packet::builder::VariantBuilder{};
  variant.function("OnTestValues")
      .param_float(1, 1.5F)
      .text(2, "text value")
      .param(3, packet::VariantValue{packet::Vec2{2.0F, 3.0F}})
      .param_u32(4, 42U)
      .param(5, packet::VariantValue{packet::Vec3{4.0F, 5.0F, 6.0F}})
      .param_i32(6, -7);
  const auto frame = variant.frame();

  RecordingHost host;
  engine.on_packet("onclientpacket", script::view_from_frame(frame, 7), host);
  assert(host.client.size() == 8);
  assert(first_message(host.client[0]) == "name:OnTestValues");
  assert(first_message(host.client[1]) == "0:string=OnTestValues");
  assert(first_message(host.client[2]) == "1:float=1.5");
  assert(first_message(host.client[3]) == "2:string=text value");
  assert(first_message(host.client[4]) == "3:vec2=2.0,3.0");
  assert(first_message(host.client[5]) == "4:uint32=42");
  assert(first_message(host.client[6]) == "5:vec3=4.0,5.0,6.0");
  assert(first_message(host.client[7]) == "6:int32=-7");
}

auto test_lua_commands() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    registerCommand("/test", "Send Hello world chat", function(args)
      send_chat("Hello world")
    end)
  )",
                     "lua_command"));

  const auto names = engine.command_names();
  assert(names.size() == 1);
  assert(names.front() == "test");

  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("test", args, host));
  assert(host.server.size() == 1);
  const auto body = first_message(host.server.front());
  assert(body == "action|input\n|text|Hello world");
}

auto test_lua_reload_request() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    registerCommand("/hotreload", "Reload scripts", function()
      requestScriptReload()
    end)
  )",
                     "reload_request"));
  assert(!engine.take_reload_request());
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("hotreload", args, host));
  assert(engine.take_reload_request());
  assert(!engine.take_reload_request());
}

auto test_items_dat_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    registerCommand("/itemsdat", function()
      local bytes, err = getItemsDat()
      send_console(bytes == nil and err or tostring(#bytes))
    end)
  )",
                     "items_dat"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("itemsdat", args, host));
  assert(host.client.size() == 1);
  assert(first_message(host.client.front()) == "items.dat not received");
}

auto test_base64_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    registerCommand("base64", function()
      send_console(base64Encode("Man"))
      send_console(base64Encode("M"))
      send_console(base64Encode("Ma"))
      send_console(base64Encode(string.rep("x", 4097)))
    end)
  )",
                     "base64"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("base64", args, host));
  assert(host.client.size() == 4);
  assert(first_message(host.client[0]) == "TWFu");
  assert(first_message(host.client[1]) == "TQ==");
  assert(first_message(host.client[2]) == "TWE=");
  assert(first_message(host.client[3]).size() == 5464);
}

auto test_schedule_callback_lua() -> void {
  auto record = std::make_shared<SharedRecord>();
  DeferredHost host{record};
  script::LuaEngine engine;
  assert(engine.load(R"(
    registerCommand("schedule", function()
      scheduleCallback(function()
        send_console("scheduled callback")
      end, 0)
    end)
  )",
                     "schedule_callback"));
  const std::vector<std::string> args;
  assert(engine.invoke_command("schedule", args, host));
  assert(record->client.empty());
  engine.poll_callback(0);
  assert(record->client.size() == 1);
  assert(first_message(record->client.front()) == "scheduled callback");
}

auto test_state_api() -> void {
  spx::GameState::instance().reset();
  spx::state::Player player;
  player.net_id = 5;
  player.user_id = 42;
  player.name = "Tester";
  spx::player_manager().upsert(player);

  spx::state::Tile tile;
  tile.x = 1;
  tile.y = 2;
  tile.foreground = 42;
  spx::tile_manager().upsert(tile);

  spx::state::WorldInfo info;
  info.name = "WORLD";
  spx::tile_manager().set_info(info);
  spx::player_manager().set_inventory(
      7, 20, std::vector<spx::state::InventoryItem>{{242, 5, 0}});

  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    addHook(function()
      local list = getPlayerList()
      if #list > 0 then send_console("player:" .. list[1].name) end
      local found = getTile(1, 2)
      if found then send_console("tile:" .. tostring(found.fg)) end
      send_console("world:" .. getWorldName())
      local inv = getInventory()
      if #inv > 0 then send_console("inv:" .. tostring(inv[1].id) .. "x" .. tostring(inv[1].count)) end
    end, "onconnect")
    registerCommand("/getinventory", "Send inventory", function(args)
      local slots = getInventorySlots()
      for index, item in ipairs(slots) do
        local name = item.empty and "(empty)" or item.name
        send_log(index .. ". " .. name .. " " .. tostring(item.count) .. "x")
      end
    end)
  )LUA",
                     "state_api"));

  RecordingHost host;
  engine.on_simple("onconnect", 1, {}, {}, host);
  assert(host.client.size() == 4);
  assert(first_message(host.client[0]) == "player:Tester");
  assert(first_message(host.client[1]) == "tile:42");
  assert(first_message(host.client[2]) == "world:WORLD");
  assert(first_message(host.client[3]) == "inv:242x5");

  host.client.clear();
  const std::vector<std::string> args;
  assert(engine.invoke_command("getinventory", args, host));
  assert(host.client.size() == 20);
  assert(first_message(host.client.front()).ends_with("5x"));
  assert(first_message(host.client.back()).find("20. (empty) 0x") !=
         std::string::npos);

  spx::GameState::instance().reset();
}

auto test_errors_and_facade() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"(
    addHook(function() error("boom") end, "onclientpacket")
  )",
                     "bad_hook"));

  RecordingHost host;
  const auto frame = text_frame("still forwarded");
  const auto reply = engine.on_packet("onclientpacket",
                                      script::view_from_frame(frame, 7), host);
  assert(reply.decision == script::HookDecision::Forward);

  assert(script_load("log('queued')", "queued"));
  assert(!script_load_file(""));
  script_clear();
}

auto test_cooperative_sleep() -> void {
  auto record = std::make_shared<SharedRecord>();
  script::LuaEngine engine;
  assert(engine.available());
  assert(engine.load(R"LUA(
    registerCommand("/slow", "Deferred", function()
      sendPacket(2, "first")
      sleep(5)
      sendPacket(2, "second")
    end)
  )LUA",
                     "cooperative_sleep"));

  DeferredHost host{record};
  const std::vector<std::string> args;
  assert(engine.invoke_command("slow", args, host));
  assert(record->server.size() == 1);
  assert(first_message(record->server[0]) == "first");

  std::this_thread::sleep_for(std::chrono::milliseconds{12});
  const auto wait = engine.poll_callback(50);
  assert(wait <= 50);
  assert(record->server.size() == 2);
  assert(first_message(record->server[1]) == "second");
}

auto test_sleep() -> void {
  script::LuaEngine engine;
  assert(engine.available());
  assert(engine.load(R"LUA(
    registerCommand("/nap", "Sleep", function()
      sleep(1)
      sleepMs(1)
    end)
  )LUA",
                     "sleep_api"));

  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("nap", args, host));
  assert(host.client.empty());
  assert(host.server.empty());
}

auto test_dialog_block() -> void {
  script::LuaEngine engine;
  assert(engine.available());
  assert(engine.load(R"LUA(
    assert(blockDialog("drop_item", 2))
    assert(blockDialog("WrenchMenu", -1))
    assert(not blockDialog("[", 1))
  )LUA",
                     "dialog_block"));

  assert(engine.consume_dialog_block("dialog_name|drop_item\nitemID|830"));
  assert(engine.consume_dialog_block("add_button|yes\nend_dialog|drop_item"));
  assert(!engine.consume_dialog_block("dialog_name|drop_item"));
  assert(engine.consume_dialog_block("WrenchMenu"));
  assert(engine.consume_dialog_block("WrenchMenu"));
  assert(!engine.consume_dialog_block("unrelated dialog"));
}

auto test_inventory_event() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    addEvent(Event.inventory, function(session, changes)
      for i, c in ipairs(changes) do
        send_log(i .. ":" .. tostring(c.slot) .. ":" .. tostring(c.oldId) ..
                 ":" .. tostring(c.newId) .. ":" .. tostring(c.newCount) ..
                 ":" .. tostring(c.empty))
      end
    end)
  )LUA",
                     "inventory_event"));

  RecordingHost host;
  const std::vector<script::InventoryChange> changes{{1, 0, 0, 242, 5},
                                                     {3, 242, 2, 0, 0}};
  engine.on_inventory(7, changes, host);
  assert(host.client.size() == 2);
  assert(first_message(host.client[0]).ends_with("msg|1:1:0:242:5:false"));
  assert(first_message(host.client[1]).ends_with("msg|2:3:242:0:0:true"));
}

auto test_set_inventory_slot() -> void {
  auto &game = GameState::instance();
  game.reset();
  game.players().set_inventory(1, 3, {{242, 5, 0}});

  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    registerCommand("/setslot", "Set slot", function()
      setInventorySlot(1, 242, 8)
    end)
  )LUA",
                     "setslot"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("setslot", args, host));
  assert(host.client.size() == 1);
  const auto payload = packet::PacketDecoder::parse_payload(host.client[0]);
  assert(payload.has_value());
  const auto *frame = packet::get_payload_if<packet::GamePayload>(*payload);
  assert(frame != nullptr);
  assert(frame->packet.type ==
         static_cast<uint8_t>(packet::PacketType::ModifyItemInventory));
  assert(frame->packet.int_data == 242);
  const auto *raw = reinterpret_cast<const uint8_t *>(&frame->packet);
  assert(raw[2] == 0 && raw[3] == 3);
  assert(game.players().inventory_slots()[0].count == 8);
  game.reset();
}

auto test_fast_drop_script() -> void {
  std::ifstream input{std::string{SPX_SCRIPT_DIR} + "/Command_FastDrop.lua",
                      std::ios::binary};
  assert(input.good());
  const std::string source{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};

  auto &game = GameState::instance();
  game.reset();
  game.players().set_inventory(1, 3, {{242, 5, 0}});

  script::LuaEngine engine;
  assert(engine.load(source, "Command_FastDrop.lua"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("fd", args, host));

  const auto frame = packet::serialize_payload(
      packet::TextPayload{2u, "action|drop\n|itemID|242"});
  const auto view = script::view_from_frame(frame, 1);
  const auto reply = engine.on_packet("onclientpacket", view, host);
  assert(reply.decision == script::HookDecision::Drop);
  assert(host.server.size() == 2);

  const auto first = packet::PacketDecoder::parse_payload(host.server[0]);
  assert(first.has_value());
  const auto *first_text = packet::get_payload_if<packet::TextPayload>(*first);
  assert(first_text != nullptr);
  assert(first_text->data == "action|drop\n|itemID|242");

  const auto second = packet::PacketDecoder::parse_payload(host.server[1]);
  assert(second.has_value());
  const auto *second_text =
      packet::get_payload_if<packet::TextPayload>(*second);
  assert(second_text != nullptr);
  assert(second_text->data ==
         "action|dialog_return\ndialog_name|drop_item\nitemID|242|\ncount|5");
  game.reset();
}

auto test_scheduler() -> void {
  spx::script::Scheduler scheduler{2};
  std::atomic<int> calls{0};
  const spx::script::TaskId first = scheduler.schedule_delayed(
      [&calls] { calls.fetch_add(1); }, std::chrono::milliseconds{5}, "unit");
  assert(first != spx::script::kInvalidTaskId);
  const spx::script::TaskId second = scheduler.schedule_delayed(
      [&calls] { calls.fetch_add(10); }, std::chrono::milliseconds{50}, "unit");
  assert(second != spx::script::kInvalidTaskId);
  assert(scheduler.cancel(second));
  assert(!scheduler.is_pending(second));
  scheduler.wait_all();
  assert(calls.load() == 1);
  assert(scheduler.pending_count() == 0);

  std::atomic<int> ticks{0};
  const spx::script::TaskId periodic = scheduler.schedule_periodic(
      [&ticks] { ticks.fetch_add(1); }, std::chrono::milliseconds{5}, "tick");
  assert(periodic != spx::script::kInvalidTaskId);
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  assert(scheduler.cancel(periodic));
  scheduler.wait_all();
  const int seen = ticks.load();
  assert(seen >= 2);
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  assert(ticks.load() == seen);
  assert(scheduler.pending_count() == 0);

  scheduler.pause();
  scheduler.resume();
  scheduler.stop();
}

auto test_fetch_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    registerCommand("/http", "Fetch a URL", function()
      local r = fetch("not-a-url")
      send_log(tostring(r.ok) .. "|" .. r.error .. "|" .. tostring(r.status))
    end)
  )LUA",
                     "http"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("http", args, host));
  assert(host.client.size() == 1);
  assert(first_message(host.client.front()).ends_with("false|invalid URL|0"));
}

auto test_fetch_lua_async() -> void {
  auto record = std::make_shared<SharedRecord>();
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    registerCommand("/httpasync", "Deferred fetch", function()
      local r = fetch("not-a-url")
      send_log(tostring(r.ok) .. "|" .. r.error)
    end)
  )LUA",
                     "http_async"));

  DeferredHost host{record};
  const std::vector<std::string> args;
  const auto start = std::chrono::steady_clock::now();
  assert(engine.invoke_command("httpasync", args, host));
  assert(record->client.empty());
  while (record->client.empty()) {
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds{5});
    engine.poll_callback(20);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  assert(record->client.size() == 1);
  assert(first_message(record->client.front()).ends_with("false|invalid URL"));
}

auto test_http_instance_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    registerCommand("/httpobj", "Instance fetch", function()
      local client = Http.new({ headers = { ["X-Test"] = "1" } })
      local a = client.get("not-a-url")
      local b = client:get("not-a-url")
      local c = client.post("not-a-url", { body = "x" })
      send_log(tostring(a.error) .. "|" .. tostring(b.error) .. "|" ..
               tostring(c.error))
    end)
  )LUA",
                     "http_instance"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("httpobj", args, host));
  assert(host.client.size() == 1);
  assert(first_message(host.client.front())
             .ends_with("invalid URL|invalid URL|invalid URL"));
}

auto test_http_socks5_config_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    registerCommand("/socks", "SOCKS5 toggle", function()
      local a = setHttpSocks5Config("127.0.0.1", 1080, "user", "pass")
      local b = setHttpSocks5(false)
      local c = setHttpSocks5Config("127.0.0.1", 0)
      send_log(tostring(a) .. "|" .. tostring(b) .. "|" .. tostring(c))
    end)
  )LUA",
                     "http_socks5"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("socks", args, host));
  assert(host.client.size() == 1);
  assert(first_message(host.client.front()).ends_with("true|false|false"));
}

auto test_device_info_lua() -> void {
  script::LuaEngine engine;
  assert(engine.load(R"LUA(
    local info = getDeviceInfo()
    registerCommand("/dev", "Report device", function()
      send_log(info.platform .. "|" .. info.arch .. "|" ..
               tostring(info.cpuCount) .. "|" .. info.hostname)
    end)
  )LUA",
                     "device"));
  RecordingHost host;
  const std::vector<std::string> args;
  assert(engine.invoke_command("dev", args, host));
  assert(host.client.size() == 1);

  const auto expected = platform::device_info();
  const std::string want = expected.platform + "|" + expected.arch + "|" +
                           std::to_string(expected.cpu_count) + "|" +
                           expected.hostname;
  assert(first_message(host.client.front()).ends_with(want));
}

auto test_warp_world_script() -> void {
  std::ifstream input{std::string{SPX_SCRIPT_DIR} + "/Command_WarpWorld.lua",
                      std::ios::binary};
  assert(input.good());
  const std::string source{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};

  script::LuaEngine engine;
  assert(engine.load(source, "Command_WarpWorld.lua"));
  RecordingHost host;
  const std::vector<std::string> args{"LUCKYOXYGEN"};
  assert(engine.invoke_command("warp", args, host));
  assert(host.server.size() == 1);

  const auto quit = packet::PacketDecoder::parse_payload(host.server[0]);
  assert(quit.has_value());
  const auto *quit_text = packet::get_payload_if<packet::TextPayload>(*quit);
  assert(quit_text != nullptr);
  assert(quit_text->message_type == 3u);
  assert(quit_text->data == "action|quit_to_exit");

  auto position = packet::builder::VariantBuilder{};
  position.function("OnSetPos").param_i32(1, 5);
  const auto position_frame = position.frame();
  const auto position_reply = engine.on_packet(
      "onvariant", script::view_from_frame(position_frame, 1), host);
  assert(position_reply.decision == script::HookDecision::Drop);
  assert(host.server.size() == 1);

  auto variant = packet::builder::VariantBuilder{};
  variant.function("OnRequestWorldSelectMenu").text(1, "worlds");
  const auto menu_frame = variant.frame();
  const auto reply = engine.on_packet(
      "onvariant", script::view_from_frame(menu_frame, 1), host);
  assert(reply.decision == script::HookDecision::Drop);
  assert(host.server.size() == 2);

  const auto join = packet::PacketDecoder::parse_payload(host.server[1]);
  assert(join.has_value());
  const auto *join_text = packet::get_payload_if<packet::TextPayload>(*join);
  assert(join_text != nullptr);
  assert(join_text->message_type == 3u);
  assert(join_text->data ==
         "action|join_request\nname|LUCKYOXYGEN\ninvitedWorld|0");
}

} // namespace

auto main() -> int {
  test_growlauncher_hooks();
  test_replacement_hook();
  test_mori_hooks_and_sends();
  test_decode_and_variant();
  test_variant_types();
  test_base64_lua();
  test_schedule_callback_lua();
  test_lua_commands();
  test_lua_reload_request();
  test_items_dat_lua();
  test_state_api();
  test_cooperative_sleep();
  test_sleep();
  test_dialog_block();
  test_inventory_event();
  test_set_inventory_slot();
  test_fast_drop_script();
  test_warp_world_script();
  test_device_info_lua();
  test_scheduler();
  test_fetch_lua();
  test_fetch_lua_async();
  test_http_instance_lua();
  test_http_socks5_config_lua();
  test_errors_and_facade();
  return 0;
}
