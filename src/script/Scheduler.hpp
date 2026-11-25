/** @file
 *  @brief Timer and worker task scheduler for asynchronous script work.
 */

#ifndef SPXCORE_SCRIPT_SCHEDULER_HPP
#define SPXCORE_SCRIPT_SCHEDULER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spx::script {

using TaskId = uint64_t;
constexpr TaskId kInvalidTaskId{0};

enum class TaskPriority : int8_t {
  Highest = -128,
  High = -64,
  Normal = 0,
  Low = 64,
  Lowest = 127,
};

/** Options attached to one scheduled task. */
struct TaskOptions {
  std::string tag;
  TaskPriority priority{TaskPriority::Normal};
  std::chrono::milliseconds delay{0};
  std::chrono::milliseconds interval{0};
};

/**
 * @brief Runs callbacks on a bounded worker pool using time based flags.
 *
 * A dedicated timer thread moves tasks whose execute time has arrived into a
 * ready queue. Up to four workers run those callbacks; Android is capped to
 * one worker to avoid CPU oversubscription.
 */
class Scheduler {
public:
#if defined(__ANDROID__)
  static constexpr size_t kMaxAndroidWorkers{1};
#endif
  explicit Scheduler(size_t num_threads = 4);
  ~Scheduler();
  Scheduler(const Scheduler &) = delete;
  auto operator=(const Scheduler &) -> Scheduler & = delete;

  auto schedule(std::function<void()> callback, const TaskOptions &options = {})
      -> TaskId;

  auto schedule_delayed(std::function<void()> callback,
                        std::chrono::milliseconds delay,
                        const std::string &tag = "",
                        TaskPriority priority = TaskPriority::Normal) -> TaskId;

  auto schedule_periodic(
      std::function<void()> callback, std::chrono::milliseconds interval,
      const std::string &tag = "", TaskPriority priority = TaskPriority::Normal,
      std::chrono::milliseconds initial_delay = std::chrono::milliseconds{0},
      std::shared_ptr<std::atomic<bool>> should_continue = nullptr) -> TaskId;

  auto schedule_immediate(std::function<void()> callback,
                          const std::string &tag = "",
                          TaskPriority priority = TaskPriority::Normal)
      -> TaskId;

  auto cancel(TaskId id) -> bool;
  auto cancel_by_tag(const std::string &tag) -> size_t;
  void cancel_all();

  [[nodiscard]] auto is_pending(TaskId id) const -> bool;
  [[nodiscard]] auto is_running(TaskId id) const -> bool;
  [[nodiscard]] auto pending_count() const -> size_t;
  [[nodiscard]] auto pending_count_by_tag(const std::string &tag) const
      -> size_t;

  void pause();
  void resume();
  void stop();
  void wait_all();

private:
  struct Task {
    TaskId id{0};
    std::string tag;
    TaskPriority priority{TaskPriority::Normal};
    std::chrono::steady_clock::time_point execute_at;
    std::chrono::milliseconds interval{0};
    std::function<void()> callback;
    std::shared_ptr<std::atomic<bool>> should_continue;

    auto operator>(const Task &other) const -> bool {
      if (execute_at != other.execute_at) {
        return execute_at > other.execute_at;
      }
      return static_cast<int8_t>(priority) >
             static_cast<int8_t>(other.priority);
    }
  };

  void worker_thread();
  void timer_thread();
  auto generate_id() -> TaskId;
  auto enqueue(Task task) -> TaskId;

  std::priority_queue<Task, std::vector<Task>, std::greater<Task>> task_queue_;
  std::unordered_set<TaskId> cancelled_tasks_;
  std::unordered_set<TaskId> pending_tasks_;
  std::unordered_set<TaskId> running_tasks_;
  std::unordered_map<std::string, std::unordered_set<TaskId>> tasks_by_tag_;
  std::queue<Task> ready_queue_;

  mutable std::mutex mutex_;
  std::condition_variable timer_cv_;
  std::condition_variable worker_cv_;
  std::condition_variable idle_cv_;

  std::vector<std::thread> workers_;
  std::thread timer_thread_;
  size_t num_threads_{1};
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<TaskId> next_id_{1};
};

} // namespace spx::script

#endif // SPXCORE_SCRIPT_SCHEDULER_HPP
