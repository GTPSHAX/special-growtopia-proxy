#include "specialproxy/state/PlayerManager.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace spx::state {

void PlayerManager::upsert(Player player) {
  std::lock_guard<std::mutex> lock{mutex_};
  players_[player.net_id] = std::move(player);
}

void PlayerManager::remove(int32_t net_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  players_.erase(net_id);
}

auto PlayerManager::find(int32_t net_id) const -> std::optional<Player> {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = players_.find(net_id);
  if (it == players_.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto PlayerManager::snapshot() const -> std::vector<Player> {
  std::lock_guard<std::mutex> lock{mutex_};
  std::vector<Player> result;
  result.reserve(players_.size());
  for (const auto &[net_id, player] : players_) {
    (void)net_id;
    result.push_back(player);
  }
  return result;
}

auto PlayerManager::first() const -> std::optional<Player> {
  std::lock_guard<std::mutex> lock{mutex_};
  if (players_.empty()) {
    return std::nullopt;
  }
  return players_.begin()->second;
}

void PlayerManager::set_inventory(uint8_t version, uint32_t slot_count,
                                  std::vector<InventoryItem> items) {
  std::lock_guard<std::mutex> lock{mutex_};
  inventory_version_ = version;
  inventory_slot_count_ = slot_count;
  inventory_ = std::move(items);
}

void PlayerManager::modify_inventory(int32_t item_id, int32_t delta) {
  std::lock_guard<std::mutex> lock{mutex_};
  for (auto it = inventory_.begin(); it != inventory_.end(); ++it) {
    if (it->id != static_cast<uint32_t>(item_id)) {
      continue;
    }
    const int32_t next = static_cast<int32_t>(it->count) + delta;
    if (next <= 0) {
      inventory_.erase(it);
    } else {
      it->count = static_cast<uint8_t>(std::min<int32_t>(next, 200));
    }
    return;
  }
  if (delta > 0 && item_id > 0) {
    inventory_.push_back(
        InventoryItem{static_cast<uint32_t>(item_id),
                      static_cast<uint8_t>(std::min<int32_t>(delta, 200)), 0});
  }
}

auto PlayerManager::inventory() const -> std::vector<InventoryItem> {
  std::lock_guard<std::mutex> lock{mutex_};
  return inventory_;
}

auto PlayerManager::inventory_slots() const -> std::vector<InventoryItem> {
  std::lock_guard<std::mutex> lock{mutex_};
  const size_t total = std::min<size_t>(inventory_slot_count_, 256u);
  std::vector<InventoryItem> slots;
  slots.reserve(std::max(total, inventory_.size()));
  for (const auto &item : inventory_) {
    slots.push_back(item);
  }
  while (slots.size() < total) {
    slots.push_back(InventoryItem{});
  }
  return slots;
}

auto PlayerManager::inventory_slot_count() const noexcept -> uint32_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return inventory_slot_count_;
}

void PlayerManager::clear() {
  std::lock_guard<std::mutex> lock{mutex_};
  players_.clear();
  inventory_.clear();
  inventory_version_ = 0;
  inventory_slot_count_ = 0;
}

auto PlayerManager::size() const noexcept -> size_t {
  std::lock_guard<std::mutex> lock{mutex_};
  return players_.size();
}

} // namespace spx::state
