#include "specialproxy/state/WorldManager.hpp"

#include <mutex>
#include <utility>

namespace spx::state {

namespace {

/** @brief Combine tile coordinates into one map key. */
auto tile_key(int32_t x, int32_t y) noexcept -> uint64_t {
  return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
         static_cast<uint32_t>(y);
}

} // namespace

void WorldManager::set_info(WorldInfo info) {
  std::lock_guard<std::mutex> lock{mutex_};
  info_ = std::move(info);
}

auto WorldManager::info() const -> WorldInfo {
  std::lock_guard<std::mutex> lock{mutex_};
  return info_;
}

void TileManager::upsert(Tile tile) {
  std::lock_guard<std::mutex> lock{mutex_};
  tiles_[tile_key(tile.x, tile.y)] = tile;
}

auto TileManager::get(int32_t x, int32_t y) const -> std::optional<Tile> {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = tiles_.find(tile_key(x, y));
  if (it == tiles_.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto TileManager::snapshot() const -> std::vector<Tile> {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<Tile> result;
  result.reserve(tiles_.size());
  for (const auto &[key, tile] : tiles_) {
    (void)key;
    result.push_back(tile);
  }
  return result;
}

auto TileManager::foreground_item(int32_t x, int32_t y) const
    -> std::optional<ItemInfo> {
  const auto tile = get(x, y);
  return tile ? item_info(tile->foreground) : std::nullopt;
}

auto TileManager::background_item(int32_t x, int32_t y) const
    -> std::optional<ItemInfo> {
  const auto tile = get(x, y);
  return tile ? item_info(tile->background) : std::nullopt;
}

void TileManager::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  tiles_.clear();
}

auto TileManager::size() const noexcept -> size_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return tiles_.size();
}

void ObjectManager::upsert(WorldObject object) {
  std::lock_guard<std::mutex> lock{mutex_};
  objects_[object.object_id] = object;
}

void ObjectManager::remove(uint32_t object_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  objects_.erase(object_id);
}

auto ObjectManager::snapshot() const -> std::vector<WorldObject> {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<WorldObject> result;
  result.reserve(objects_.size());
  for (const auto &[id, object] : objects_) {
    (void)id;
    result.push_back(object);
  }
  return result;
}

void ObjectManager::set_drop_id(uint32_t value) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  drop_id_ = value;
}

auto ObjectManager::allocate_drop_id() -> uint32_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return ++drop_id_;
}

void ObjectManager::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  objects_.clear();
}

auto ObjectManager::size() const noexcept -> size_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return objects_.size();
}

void NpcManager::upsert(Npc npc) {
  std::lock_guard<std::mutex> lock{mutex_};
  npcs_[npc.net_id] = npc;
}

void NpcManager::remove(int32_t net_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  npcs_.erase(net_id);
}

auto NpcManager::snapshot() const -> std::vector<Npc> {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<Npc> result;
  result.reserve(npcs_.size());
  for (const auto &[id, npc] : npcs_) {
    (void)id;
    result.push_back(npc);
  }
  return result;
}

void NpcManager::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  npcs_.clear();
}

auto NpcManager::size() const noexcept -> size_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return npcs_.size();
}

} // namespace spx::state
