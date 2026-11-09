#include <cstdint>
#include <set>
#include <vector>

#include "aether/foundation/memory.hpp"
#include "aether_test.hpp"

using namespace aether;

namespace {
bool aligned(const void* p, std::size_t a) { return reinterpret_cast<std::uintptr_t>(p) % a == 0; }
}  // namespace

AETHER_TEST(heap_tracks_live_allocations) {
    HeapAllocator heap;
    void* a = heap.allocate(64);
    void* b = heap.allocate(128, 64);
    REQUIRE(a && b);
    CHECK(aligned(b, 64));
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{2});
    CHECK_EQ(heap.stats().live_bytes, std::uint64_t{192});
    heap.deallocate(a, 64);
    heap.deallocate(b, 128, 64);
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
    CHECK_EQ(heap.stats().live_bytes, std::uint64_t{0});
    CHECK_EQ(heap.stats().peak_bytes, std::uint64_t{192});
}

AETHER_TEST(heap_create_destroy) {
    HeapAllocator heap;
    struct Obj { int x; explicit Obj(int v) : x(v) {} };
    Obj* o = heap.create<Obj>(42);
    REQUIRE(o);
    CHECK_EQ(o->x, 42);
    heap.destroy(o);
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
}

AETHER_TEST(arena_bumps_and_aligns) {
    HeapAllocator heap;
    {
        ArenaAllocator arena(1024, heap);
        void* a = arena.allocate(3, 1);
        void* b = arena.allocate(16, 16);
        REQUIRE(a && b);
        CHECK(aligned(b, 16));
        CHECK(static_cast<std::byte*>(b) > static_cast<std::byte*>(a));
        CHECK(arena.used() <= 32);
    }
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
}

AETHER_TEST(arena_returns_null_when_full) {
    alignas(16) std::byte buffer[64];
    ArenaAllocator arena(buffer, sizeof(buffer));
    CHECK(arena.allocate(64, 1) != nullptr);
    CHECK(arena.allocate(1, 1) == nullptr);
    arena.reset();
    CHECK(arena.allocate(65, 1) == nullptr);
    CHECK(arena.allocate(SIZE_MAX, 1) == nullptr);
    CHECK(arena.allocate(32, 1) != nullptr);
}

AETHER_TEST(arena_marker_rewind) {
    HeapAllocator heap;
    ArenaAllocator arena(256, heap);
    (void)arena.allocate(16);
    const auto mark = arena.marker();
    (void)arena.allocate(100);
    CHECK(arena.used() > mark);
    arena.rewind(mark);
    CHECK_EQ(arena.used(), mark);
    CHECK(arena.high_water() >= mark + 100);
}

AETHER_TEST(frame_allocator_rotates) {
    HeapAllocator heap;
    FrameAllocator frames(256, 2, heap);
    void* f0 = frames.allocate(32);
    REQUIRE(f0);
    frames.begin_frame();
    void* f1 = frames.allocate(32);
    REQUIRE(f1);
    CHECK(f0 != f1);
    CHECK_EQ(frames.frame_number(), std::uint64_t{1});
    frames.begin_frame();  // back to arena 0, which is reset
    void* f2 = frames.allocate(32);
    CHECK(f2 == f0);
}

AETHER_TEST(pool_allocates_distinct_blocks_and_reuses) {
    HeapAllocator heap;
    {
        PoolAllocator pool(24, 8, heap, 16);
        CHECK_EQ(pool.block_size(), std::size_t{32});
        std::set<void*> seen;
        std::vector<void*> blocks;
        for (int i = 0; i < 8; ++i) {
            void* p = pool.allocate(24, 16);
            REQUIRE(p);
            CHECK(aligned(p, 16));
            CHECK(pool.owns(p));
            seen.insert(p);
            blocks.push_back(p);
        }
        CHECK_EQ(seen.size(), std::size_t{8});
        CHECK(pool.allocate(24) == nullptr);
        CHECK_EQ(pool.free_count(), std::size_t{0});
        pool.deallocate(blocks[3], 24);
        CHECK(pool.allocate(24) == blocks[3]);
        CHECK(pool.allocate(64) == nullptr);  // too large
        int outside = 0;
        CHECK(!pool.owns(&outside));
    }
    CHECK_EQ(heap.stats().live_allocations, std::uint64_t{0});
}
