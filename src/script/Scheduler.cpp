/** @file
 *  @brief Timer and worker task scheduler for asynchronous script work.
 */

#include "Scheduler.hpp"

#include "specialproxy/utils/Logger.hpp"

namespace spx::script {

Scheduler::Scheduler(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = 1;
  }
#if defined(__ANDROID__)
  if (num_threads > kMaxAndroidWorkers) {
    num_threads = kMaxAndroidWorkers;
  }
#endif
  num_threads_ = num_threads;
  running_ = true;
  workers_.reserve(num_threads_);
  for (size_t i = 0; i < num_threads_; ++i) {
    workers_.emplace_back([this] { worker_thread(); });
  }
  timer_thread_ = std::thread([this] { timer_thread(); });
  spx::log::infof("script", "scheduler started with %zu worker threads",
                  num_threads_);
}

Scheduler::~Scheduler() { stop(); }

auto Scheduler::generate_id() -> TaskId {
  return next_id_.fetch_add(1, std::memory_order_relaxed);
}

auto Scheduler::enqueue(Task task) -> TaskId {
  const TaskId id = task.id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return kInvalidTaskId;
    }
    pending_tasks_.insert(id);
    if (!task.tag.empty()) {
      tasks_by_tag_[task.tag].insert(id);
    }
    task_queue_.push(std::move(task));
  }
  timer_cv_.notify_one();
  return id;
}

auto Scheduler::schedule(std::function<void()> callback,
                         const TaskOptions &options) -> TaskId {
  if (!callback) {
    return kInvalidTaskId;
  }
  Task task;
  task.id = generate_id();
  task.tag = options.tag;
  task.priority = options.priority;
  task.execute_at = std::chrono::steady_clock::now() + options.delay;
  task.interval = options.interval;
  task.callback = std::move(callback);
  return enqueue(std::move(task));
}

auto Scheduler::schedule_delayed(std::function<void()> callback,
                                 std::chrono::milliseconds delay,
                                 const std::string &tag, TaskPriority priority)
    -> TaskId {
  TaskOptions options;
  options.tag = tag;
  options.priority = priority;
  options.delay = delay;
  return schedule(std::move(callback), options);
}

auto Scheduler::schedule_periodic(
    std::function<void()> callback, std::chrono::milliseconds interval,
    const std::string &tag, TaskPriority priority,
    std::chrono::milliseconds initial_delay,
    std::shared_ptr<std::atomic<bool>> should_continue) -> TaskId {
  if (!callback) {
    return kInvalidTaskId;
  }
  Task task;
  task.id = generate_id();
  task.tag = tag;
  task.priority = priority;
  task.execute_at = std::chrono::steady_clock::now() + initial_delay;
  task.interval = interval;
  task.callback = std::move(callback);
  task.should_continue = std::move(should_continue);
  return enqueue(std::move(task));
}

auto Scheduler::schedule_immediate(std::function<void()> callback,
                                   const std::string &tag,
                                   TaskPriority priority) -> TaskId {
  return schedule_delayed(std::move(callback), std::chrono::milliseconds{0},
                          tag, priority);
}

auto Scheduler::cancel(TaskId id) -> bool {
  if (id == kInvalidTaskId) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!pending_tasks_.contains(id)) {
    return false;
  }
  cancelled_tasks_.insert(id);
  pending_tasks_.erase(id);
  if (pending_tasks_.empty() && running_tasks_.empty()) {
    idle_cv_.notify_all();
  }
  return true;
}

auto Scheduler::cancel_by_tag(const std::string &tag) -> size_t {
  if (tag.empty()) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_by_tag_.find(tag);
  if (it == tasks_by_tag_.end()) {
    return 0;
  }
  size_t count{0};
  for (TaskId id : it->second) {
    if (pending_tasks_.contains(id) || running_tasks_.contains(id)) {
      cancelled_tasks_.insert(id);
      pending_tasks_.erase(id);
      ++count;
    }
  }
  if (pending_tasks_.empty() && running_tasks_.empty()) {
    idle_cv_.notify_all();
  }
  return count;
}

void Scheduler::cancel_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (TaskId id : pending_tasks_) {
    cancelled_tasks_.insert(id);
  }
  pending_tasks_.clear();
  if (running_tasks_.empty()) {
    idle_cv_.notify_all();
  }
}

auto Scheduler::is_pending(TaskId id) const -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_tasks_.contains(id) && !cancelled_tasks_.contains(id);
}

auto Scheduler::is_running(TaskId id) const -> bool {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_tasks_.contains(id);
}

auto Scheduler::pending_count() const -> size_t {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_tasks_.size();
}

auto Scheduler::pending_count_by_tag(const std::string &tag) const -> size_t {
  if (tag.empty()) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_by_tag_.find(tag);
  if (it == tasks_by_tag_.end()) {
    return 0;
  }
  size_t count{0};
  for (TaskId id : it->second) {
    if (pending_tasks_.contains(id)) {
      ++count;
    }
  }
  return count;
}

void Scheduler::pause() {
  paused_ = true;
  timer_cv_.notify_all();
}

void Scheduler::resume() {
  paused_ = false;
  timer_cv_.notify_all();
  worker_cv_.notify_all();
}

void Scheduler::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
      return;
    }
    running_ = false;
  }
  timer_cv_.notify_all();
  worker_cv_.notify_all();
  if (timer_thread_.joinable()) {
    timer_thread_.join();
  }
  for (auto &worker_thread : workers_) {
    if (worker_thread.joinable()) {
      worker_thread.join();
    }
  }
  spx::log::infof("script", "scheduler stopped");
}

void Scheduler::wait_all() {
  std::unique_lock<std::mutex> lock(mutex_);
  idle_cv_.wait(lock, [this] {
    return pending_tasks_.empty() && running_tasks_.empty();
  });
}

void Scheduler::timer_thread() {
  while (running_) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (task_queue_.empty()) {
      timer_cv_.wait(lock,
                     [this] { return !running_ || !task_queue_.empty(); });
      if (!running_) {
        break;
      }
    }
    if (paused_) {
      timer_cv_.wait(lock, [this] { return !running_ || !paused_; });
      if (!running_) {
        break;
      }
      continue;
    }
    if (task_queue_.empty()) {
      continue;
    }

    // Copy the deadline because the queue may reallocate while waiting.
    const auto due = task_queue_.top().execute_at;
    if (due > std::chrono::steady_clock::now()) {
      timer_cv_.wait_until(lock, due);
      continue;
    }

    Task task = task_queue_.top();
    task_queue_.pop();
    if (cancelled_tasks_.contains(task.id)) {
      cancelled_tasks_.erase(task.id);
      if (!task.tag.empty()) {
        auto it = tasks_by_tag_.find(task.tag);
        if (it != tasks_by_tag_.end()) {
          it->second.erase(task.id);
          if (it->second.empty()) {
            tasks_by_tag_.erase(it);
          }
        }
      }
      continue;
    }
    ready_queue_.push(std::move(task));
    worker_cv_.notify_one();
  }
}

void Scheduler::worker_thread() {
  while (running_) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      worker_cv_.wait(lock,
                      [this] { return !running_ || !ready_queue_.empty(); });
      if (!running_) {
        break;
      }
      if (ready_queue_.empty()) {
        continue;
      }
      task = std::move(ready_queue_.front());
      ready_queue_.pop();
      if (cancelled_tasks_.contains(task.id)) {
        cancelled_tasks_.erase(task.id);
        pending_tasks_.erase(task.id);
        if (!task.tag.empty()) {
          auto it = tasks_by_tag_.find(task.tag);
          if (it != tasks_by_tag_.end()) {
            it->second.erase(task.id);
            if (it->second.empty()) {
              tasks_by_tag_.erase(it);
            }
          }
        }
        if (pending_tasks_.empty() && running_tasks_.empty()) {
          idle_cv_.notify_all();
        }
        continue;
      }
      pending_tasks_.erase(task.id);
      running_tasks_.insert(task.id);
    }
    try {
      if (task.callback) {
        task.callback();
      }
    } catch (const std::exception &error) {
      spx::log::warningf("script", "scheduled task failed - %s", error.what());
    } catch (...) {
      spx::log::warningf("script", "scheduled task failed - unknown error");
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_tasks_.erase(task.id);
      if (!task.tag.empty()) {
        auto it = tasks_by_tag_.find(task.tag);
        if (it != tasks_by_tag_.end()) {
          it->second.erase(task.id);
          if (it->second.empty()) {
            tasks_by_tag_.erase(it);
          }
        }
      }
      const bool reschedule =
          task.interval.count() > 0 && !cancelled_tasks_.contains(task.id) &&
          (!task.should_continue || task.should_continue->load());
      if (reschedule) {
        Task next;
        next.id = task.id;
        next.tag = task.tag;
        next.priority = task.priority;
        next.execute_at = std::chrono::steady_clock::now() + task.interval;
        next.interval = task.interval;
        next.callback = std::move(task.callback);
        next.should_continue = std::move(task.should_continue);
        pending_tasks_.insert(next.id);
        if (!next.tag.empty()) {
          tasks_by_tag_[next.tag].insert(next.id);
        }
        task_queue_.push(std::move(next));
        timer_cv_.notify_one();
      }
      cancelled_tasks_.erase(task.id);
      if (pending_tasks_.empty() && running_tasks_.empty()) {
        idle_cv_.notify_all();
      }
    }
  }
}

} // namespace spx::script
