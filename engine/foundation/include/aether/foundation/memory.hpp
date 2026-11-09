// Memory allocators.
//
//  - Allocator:       abstract interface used across module boundaries.
//  - HeapAllocator:   general purpose, thread safe, with live-allocation stats.
//  - ArenaAllocator:  linear bump allocator over a fixed block; reset or
//                     rewind to a marker. Not thread safe.
//  - FrameAllocator:  N rotating arenas for per-frame scratch memory.
//  - PoolAllocator:   fixed-size blocks with an intrusive free list.
//                     Not thread safe.
//
// Allocation failure returns nullptr; callers decide how to recover.
#pragma once

#include <atomic>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace aether {

inline constexpr std::size_t kDefaultAlignment = alignof(std::max_align_t);

[[nodiscard]] constexpr bool is_power_of_two(std::size_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

[[nodiscard]] constexpr std::uintptr_t align_up(std::uintptr_t v, std::size_t align) noexcept {
    return (v + (align - 1)) & ~(static_cast<std::uintptr_t>(align) - 1);
}

class Allocator {
public:
    virtual ~Allocator() = default;
    [[nodiscard]] virtual void* allocate(std::size_t size, std::size_t align = kDefaultAlignment) noexcept = 0;
    virtual void deallocate(void* ptr, std::size_t size, std::size_t align = kDefaultAlignment) noexcept = 0;

    // Constructs a T in memory from this allocator. Returns nullptr on failure.
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return mem ? ::new (mem) T(std::forward<Args>(args)...) : nullptr;
    }

    template <typename T>
    void destroy(T* obj) noexcept {
        if (!obj) return;
        obj->~T();
        deallocate(obj, sizeof(T), alignof(T));
    }
};

struct HeapStats {
    std::uint64_t live_allocations = 0;
    std::uint64_t live_bytes = 0;
    std::uint64_t total_allocations = 0;
    std::uint64_t peak_bytes = 0;
};

class HeapAllocator final : public Allocator {
public:
    [[nodiscard]] void* allocate(std::size_t size, std::size_t align = kDefaultAlignment) noexcept override;
    void deallocate(void* ptr, std::size_t size, std::size_t align = kDefaultAlignment) noexcept override;
    [[nodiscard]] HeapStats stats() const noexcept;

private:
    std::atomic<std::uint64_t> live_allocations_{0};
    std::atomic<std::uint64_t> live_bytes_{0};
    std::atomic<std::uint64_t> total_allocations_{0};
    std::atomic<std::uint64_t> peak_bytes_{0};
};

class ArenaAllocator final : public Allocator {
public:
    using Marker = std::size_t;

    // Owns a block of `capacity` bytes taken from `backing`.
    ArenaAllocator(std::size_t capacity, Allocator& backing);
    // Uses caller-owned memory; the buffer must outlive the arena.
    ArenaAllocator(void* buffer, std::size_t capacity) noexcept;
    ~ArenaAllocator() override;

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    [[nodiscard]] void* allocate(std::size_t size, std::size_t align = kDefaultAlignment) noexcept override;
    // Individual frees are no-ops; memory is reclaimed by reset() or rewind().
    void deallocate(void*, std::size_t, std::size_t) noexcept override {}

    [[nodiscard]] Marker marker() const noexcept { return offset_; }
    void rewind(Marker m) noexcept;
    void reset() noexcept { offset_ = 0; }

    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t high_water() const noexcept { return high_water_; }

private:
    std::byte* base_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
    std::size_t high_water_ = 0;
    Allocator* backing_ = nullptr;  // non-null when the arena owns base_
};

class FrameAllocator {
public:
    // `frames_in_flight` arenas of `bytes_per_frame` each. Memory allocated in
    // frame N stays valid until begin_frame() has been called
    // `frames_in_flight` more times.
    FrameAllocator(std::size_t bytes_per_frame, std::size_t frames_in_flight, Allocator& backing);

    void begin_frame() noexcept;
    [[nodiscard]] ArenaAllocator& current() noexcept { return *arenas_[index_]; }
    [[nodiscard]] void* allocate(std::size_t size, std::size_t align = kDefaultAlignment) noexcept {
        return current().allocate(size, align);
    }
    [[nodiscard]] std::uint64_t frame_number() const noexcept { return frame_; }

private:
    std::vector<std::unique_ptr<ArenaAllocator>> arenas_;
    std::size_t index_ = 0;
    std::uint64_t frame_ = 0;
};

class PoolAllocator final : public Allocator {
public:
    PoolAllocator(std::size_t block_size, std::size_t block_count, Allocator& backing,
                  std::size_t align = kDefaultAlignment);
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // `size` must be <= block_size() and `align` <= the pool alignment.
    [[nodiscard]] void* allocate(std::size_t size, std::size_t align = kDefaultAlignment) noexcept override;
    void deallocate(void* ptr, std::size_t size, std::size_t align = kDefaultAlignment) noexcept override;

    [[nodiscard]] std::size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return block_count_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return free_count_; }
    [[nodiscard]] bool owns(const void* ptr) const noexcept;

private:
    struct FreeNode { FreeNode* next; };
    std::byte* base_ = nullptr;
    std::size_t block_size_ = 0;
    std::size_t block_count_ = 0;
    std::size_t align_ = 0;
    std::size_t free_count_ = 0;
    FreeNode* free_list_ = nullptr;
    Allocator& backing_;
};

}  // namespace aether
