// Foundation micro-benchmarks. Prints one JSON object per benchmark so
// results can be archived and compared across commits and machines.
// Always publish the machine, OS, compiler and build type with results.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aether/foundation/job_system.hpp"
#include "aether/foundation/memory.hpp"
#include "aether/foundation/platform.hpp"
#include "aether/foundation/time.hpp"

using namespace aether;

namespace {

template <typename Fn>
double best_of(int runs, Fn&& fn) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        Stopwatch sw;
        fn();
        best = std::min(best, static_cast<double>(sw.elapsed_ns()));
    }
    return best;
}

void report(const char* name, double ns, double items) {
    std::printf("{\"bench\":\"%s\",\"best_ns\":%.0f,\"ns_per_item\":%.2f,\"items\":%.0f}\n",
                name, ns, ns / items, items);
}

}  // namespace

int main() {
    std::printf("{\"os\":\"%.*s\",\"hardware_threads\":%u}\n",
                static_cast<int>(platform::os_name().size()), platform::os_name().data(),
                platform::hardware_threads());

    constexpr int kAllocs = 100'000;
    std::vector<void*> ptrs(kAllocs);

    report("malloc_free_64b", best_of(5, [&] {
        for (auto& p : ptrs) p = std::malloc(64);
        for (auto* p : ptrs) std::free(p);
    }), kAllocs);

    HeapAllocator heap;
    ArenaAllocator arena(std::size_t{kAllocs} * 64, heap);
    report("arena_alloc_reset_64b", best_of(5, [&] {
        for (auto& p : ptrs) p = arena.allocate(64);
        arena.reset();
    }), kAllocs);

    PoolAllocator pool(64, kAllocs, heap);
    report("pool_alloc_free_64b", best_of(5, [&] {
        for (auto& p : ptrs) p = pool.allocate(64);
        for (auto* p : ptrs) pool.deallocate(p, 64);
    }), kAllocs);

    JobSystem jobs;
    constexpr int kJobs = 100'000;
    report("job_submit_wait_empty", best_of(5, [&] {
        WaitGroup g;
        for (int i = 0; i < kJobs; ++i) jobs.submit([] {}, &g);
        jobs.wait(g);
    }), kJobs);

    constexpr std::size_t kItems = 4'000'000;
    std::vector<float> data(kItems, 1.0f);
    report("parallel_for_4m_floats", best_of(5, [&] {
        jobs.parallel_for(0, kItems, 16'384, [&](std::size_t i) { data[i] = data[i] * 1.0001f + 0.5f; });
    }), kItems);
    return 0;
}
