// Work-stealing job system.
//
// Each worker owns a deque: it pushes and pops its own jobs at the back
// (LIFO, cache friendly) while idle workers steal from the front of other
// deques (FIFO, oldest and usually largest work first). Jobs submitted from
// threads outside the pool go to a shared injection queue.
//
// Waiting is cooperative: JobSystem::wait() runs pending jobs on the
// calling thread until the WaitGroup reaches zero, so nested waits inside
// jobs cannot deadlock the pool.
//
// Jobs must not throw. An exception escaping a job terminates the process.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

namespace aether {

using Job = std::function<void()>;

// Counts outstanding jobs. Must outlive every job submitted against it.
class WaitGroup {
public:
    [[nodiscard]] bool done() const noexcept { return pending_.load(std::memory_order_acquire) == 0; }
    [[nodiscard]] std::uint32_t pending() const noexcept { return pending_.load(std::memory_order_acquire); }

private:
    friend class JobSystem;
    std::atomic<std::uint32_t> pending_{0};
};

struct JobSystemDesc {
    // 0 means "hardware threads - 1" (at least 1).
    unsigned worker_count = 0;
    std::string thread_name_prefix = "aether-worker";
};

struct JobSystemStats {
    std::uint64_t executed = 0;
    std::uint64_t stolen = 0;
};

class JobSystem {
public:
    explicit JobSystem(JobSystemDesc desc = {});
    // Finishes every queued job, then joins the workers.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(Job job, WaitGroup* group = nullptr);

    // Runs jobs on the calling thread until `group` is done.
    void wait(WaitGroup& group);

    // Calls fn(i) for every i in [begin, end), in chunks of at most `grain`.
    // Blocks until all iterations finish.
    template <typename Fn>
    void parallel_for(std::size_t begin, std::size_t end, std::size_t grain, Fn&& fn) {
        if (begin >= end) return;
        if (grain == 0) grain = 1;
        WaitGroup group;
        for (std::size_t chunk = begin; chunk < end; chunk += grain) {
            const std::size_t chunk_end = (end - chunk > grain) ? chunk + grain : end;
            submit([&fn, chunk, chunk_end] {
                for (std::size_t i = chunk; i < chunk_end; ++i) fn(i);
            }, &group);
        }
        wait(group);
    }

    [[nodiscard]] unsigned worker_count() const noexcept { return static_cast<unsigned>(workers_.size()); }
    [[nodiscard]] JobSystemStats stats() const noexcept;

private:
    struct Task {
        Job fn;
        WaitGroup* group = nullptr;
    };

    struct alignas(64) WorkerQueue {
        std::mutex mutex;
        std::deque<Task> tasks;
    };

    void worker_main(unsigned index) noexcept;
    bool try_run_one(int self_index);
    bool pop_task(int self_index, Task& out);
    void run_task(Task& task) noexcept;

    std::vector<std::unique_ptr<WorkerQueue>> queues_;  // one per worker
    WorkerQueue injection_;                             // external submissions
    std::vector<std::thread> workers_;

    std::mutex sleep_mutex_;
    std::condition_variable wake_;
    std::atomic<std::int64_t> queued_{0};   // jobs in any queue (may dip below 0 briefly)
    std::atomic<std::int32_t> sleepers_{0}; // workers blocked on wake_
    bool stopping_ = false;  // guarded by sleep_mutex_

    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> stolen_{0};
};

}  // namespace aether
