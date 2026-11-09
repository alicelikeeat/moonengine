#include <atomic>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether_test.hpp"

using namespace aether;

AETHER_TEST(jobs_run_and_wait) {
    JobSystem jobs({.worker_count = 4});
    CHECK_EQ(jobs.worker_count(), 4u);
    std::atomic<int> counter{0};
    WaitGroup group;
    for (int i = 0; i < 1000; ++i)
        jobs.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }, &group);
    jobs.wait(group);
    CHECK(group.done());
    CHECK_EQ(counter.load(), 1000);
}

AETHER_TEST(parallel_for_covers_every_index_once) {
    JobSystem jobs({.worker_count = 3});
    std::vector<std::atomic<int>> hits(10'007);
    jobs.parallel_for(0, hits.size(), 64, [&](std::size_t i) { hits[i].fetch_add(1); });
    bool all_once = true;
    for (auto& h : hits) all_once = all_once && h.load() == 1;
    CHECK(all_once);
}

AETHER_TEST(parallel_for_handles_empty_and_zero_grain) {
    JobSystem jobs({.worker_count = 2});
    int calls = 0;
    jobs.parallel_for(5, 5, 4, [&](std::size_t) { ++calls; });
    CHECK_EQ(calls, 0);
    std::atomic<int> n{0};
    jobs.parallel_for(0, 10, 0, [&](std::size_t) { n.fetch_add(1); });
    CHECK_EQ(n.load(), 10);
}

// Recursive fork/join: every level waits on its children from inside a job.
// With a non-cooperative wait this deadlocks once depth exceeds worker count.
namespace {
std::uint64_t fib(JobSystem& jobs, int n) {
    if (n < 12) {
        std::uint64_t a = 0, b = 1;
        for (int i = 0; i < n; ++i) { const auto t = a + b; a = b; b = t; }
        return a;
    }
    std::uint64_t x = 0, y = 0;
    WaitGroup group;
    jobs.submit([&] { x = fib(jobs, n - 1); }, &group);
    jobs.submit([&] { y = fib(jobs, n - 2); }, &group);
    jobs.wait(group);
    return x + y;
}
}  // namespace

AETHER_TEST(nested_waits_do_not_deadlock) {
    JobSystem jobs({.worker_count = 2});
    CHECK_EQ(fib(jobs, 24), std::uint64_t{46368});
}

AETHER_TEST(stress_many_external_submitters) {
    JobSystem jobs({.worker_count = 4});
    std::atomic<std::uint64_t> sum{0};
    constexpr int kThreads = 6;
    constexpr int kJobsPerThread = 5000;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&jobs, &sum] {
            WaitGroup group;
            for (int i = 1; i <= kJobsPerThread; ++i)
                jobs.submit([&sum, i] { sum.fetch_add(static_cast<std::uint64_t>(i)); }, &group);
            jobs.wait(group);
        });
    }
    for (auto& p : producers) p.join();
    const std::uint64_t per_thread = std::uint64_t{kJobsPerThread} * (kJobsPerThread + 1) / 2;
    CHECK_EQ(sum.load(), per_thread * kThreads);
    CHECK(jobs.stats().executed >= std::uint64_t{kThreads} * kJobsPerThread);
}

AETHER_TEST(jobs_spawning_jobs_are_stolen) {
    JobSystem jobs({.worker_count = 4});
    std::atomic<int> leaves{0};
    WaitGroup group;
    jobs.submit([&] {
        // All children land on one worker's local queue; others must steal.
        for (int i = 0; i < 2000; ++i)
            jobs.submit([&] {
                volatile int spin = 0;
                for (int k = 0; k < 200; ++k) spin = spin + k;
                leaves.fetch_add(1);
            }, &group);
    }, &group);
    jobs.wait(group);
    CHECK_EQ(leaves.load(), 2000);
    // Stealing is timing dependent, so only report it; correctness is above.
    std::printf("    stolen=%llu executed=%llu\n",
                static_cast<unsigned long long>(jobs.stats().stolen),
                static_cast<unsigned long long>(jobs.stats().executed));
}

AETHER_TEST(destructor_drains_queued_jobs) {
    std::atomic<int> ran{0};
    {
        JobSystem jobs({.worker_count = 2});
        for (int i = 0; i < 500; ++i) jobs.submit([&ran] { ran.fetch_add(1); });
    }
    CHECK_EQ(ran.load(), 500);
}

AETHER_TEST(repeated_create_destroy) {
    for (int round = 0; round < 50; ++round) {
        JobSystem jobs({.worker_count = 3});
        WaitGroup group;
        std::atomic<int> n{0};
        for (int i = 0; i < 20; ++i) jobs.submit([&n] { n.fetch_add(1); }, &group);
        jobs.wait(group);
        CHECK_EQ(n.load(), 20);
    }
}
