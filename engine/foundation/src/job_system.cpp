#include "aether/foundation/job_system.hpp"

#include <algorithm>

#include "aether/foundation/platform.hpp"

namespace aether {
namespace {

// Identifies the job system (and worker slot) that owns the current thread.
thread_local const JobSystem* tl_owner = nullptr;
thread_local int tl_worker_index = -1;

}  // namespace

JobSystem::JobSystem(JobSystemDesc desc) {
    unsigned count = desc.worker_count;
    if (count == 0) count = std::max(1u, platform::hardware_threads() - 1);

    queues_.reserve(count);
    for (unsigned i = 0; i < count; ++i) queues_.push_back(std::make_unique<WorkerQueue>());

    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        workers_.emplace_back([this, i, name = desc.thread_name_prefix + "-" + std::to_string(i)] {
            platform::set_current_thread_name(name);
            worker_main(i);
        });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(sleep_mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

void JobSystem::submit(Job job, WaitGroup* group) {
    if (group) group->pending_.fetch_add(1, std::memory_order_relaxed);

    WorkerQueue& queue = (tl_owner == this && tl_worker_index >= 0)
                             ? *queues_[static_cast<std::size_t>(tl_worker_index)]
                             : injection_;
    {
        std::lock_guard lock(queue.mutex);
        queue.tasks.push_back(Task{std::move(job), group});
    }
    // Sleep/wake handshake (see worker_main): both sides use seq_cst so that
    // either the worker sees this job before sleeping, or we see the sleeper.
    queued_.fetch_add(1, std::memory_order_seq_cst);
    if (sleepers_.load(std::memory_order_seq_cst) > 0) {
        // Taking the mutex orders us after a sleeper's check-then-wait.
        { std::lock_guard lock(sleep_mutex_); }
        wake_.notify_one();
    }
}

void JobSystem::wait(WaitGroup& group) {
    const int self = (tl_owner == this) ? tl_worker_index : -1;
    while (!group.done()) {
        if (!try_run_one(self)) std::this_thread::yield();
    }
}

JobSystemStats JobSystem::stats() const noexcept {
    return JobSystemStats{executed_.load(std::memory_order_relaxed),
                          stolen_.load(std::memory_order_relaxed)};
}

void JobSystem::worker_main(unsigned index) noexcept {
    tl_owner = this;
    tl_worker_index = static_cast<int>(index);

    constexpr int kSpinsBeforeSleep = 64;
    int idle_spins = 0;
    for (;;) {
        if (try_run_one(static_cast<int>(index))) {
            idle_spins = 0;
            continue;
        }
        if (++idle_spins < kSpinsBeforeSleep) {
            std::this_thread::yield();
            continue;
        }
        idle_spins = 0;

        std::unique_lock lock(sleep_mutex_);
        sleepers_.fetch_add(1, std::memory_order_seq_cst);
        while (!stopping_ && queued_.load(std::memory_order_seq_cst) <= 0) wake_.wait(lock);
        sleepers_.fetch_sub(1, std::memory_order_seq_cst);
        if (stopping_ && queued_.load(std::memory_order_seq_cst) <= 0) break;
    }

    tl_owner = nullptr;
    tl_worker_index = -1;
}

bool JobSystem::try_run_one(int self_index) {
    Task task;
    if (!pop_task(self_index, task)) return false;
    run_task(task);
    return true;
}

bool JobSystem::pop_task(int self_index, Task& out) {
    const auto take = [&](WorkerQueue& q, bool from_back) {
        std::lock_guard lock(q.mutex);
        if (q.tasks.empty()) return false;
        if (from_back) {
            out = std::move(q.tasks.back());
            q.tasks.pop_back();
        } else {
            out = std::move(q.tasks.front());
            q.tasks.pop_front();
        }
        return true;
    };

    bool found = false;
    // 1. Own queue, newest first.
    if (self_index >= 0) found = take(*queues_[static_cast<std::size_t>(self_index)], true);
    // 2. Work submitted from outside the pool.
    if (!found) found = take(injection_, false);
    // 3. Steal the oldest job from another worker.
    if (!found) {
        const std::size_t n = queues_.size();
        const std::size_t start = self_index >= 0 ? static_cast<std::size_t>(self_index) + 1 : 0;
        for (std::size_t k = 0; k < n && !found; ++k) {
            const std::size_t victim = (start + k) % n;
            if (static_cast<int>(victim) == self_index) continue;
            if (take(*queues_[victim], false)) {
                found = true;
                stolen_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (found) queued_.fetch_sub(1, std::memory_order_acq_rel);
    return found;
}

void JobSystem::run_task(Task& task) noexcept {
    task.fn();  // An exception here reaches noexcept and terminates, by design.
    task.fn = nullptr;  // Release captures before signalling completion.
    executed_.fetch_add(1, std::memory_order_relaxed);
    if (task.group) task.group->pending_.fetch_sub(1, std::memory_order_acq_rel);
    // `task.group` may be destroyed by its waiter from here on.
}

}  // namespace aether
