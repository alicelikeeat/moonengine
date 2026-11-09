#include "aether/foundation/memory.hpp"

#include "aether/foundation/assert.hpp"

namespace aether {

// ---- HeapAllocator ---------------------------------------------------------

void* HeapAllocator::allocate(std::size_t size, std::size_t align) noexcept {
    AETHER_ASSERT(is_power_of_two(align), "alignment must be a power of two");
    if (size == 0) size = 1;
    void* ptr = ::operator new(size, std::align_val_t{align}, std::nothrow);
    if (!ptr) return nullptr;

    live_allocations_.fetch_add(1, std::memory_order_relaxed);
    total_allocations_.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t now = live_bytes_.fetch_add(size, std::memory_order_relaxed) + size;
    std::uint64_t peak = peak_bytes_.load(std::memory_order_relaxed);
    while (now > peak && !peak_bytes_.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
    }
    return ptr;
}

void HeapAllocator::deallocate(void* ptr, std::size_t size, std::size_t align) noexcept {
    if (!ptr) return;
    if (size == 0) size = 1;
    ::operator delete(ptr, std::align_val_t{align});
    live_allocations_.fetch_sub(1, std::memory_order_relaxed);
    live_bytes_.fetch_sub(size, std::memory_order_relaxed);
}

HeapStats HeapAllocator::stats() const noexcept {
    return HeapStats{live_allocations_.load(), live_bytes_.load(), total_allocations_.load(),
                     peak_bytes_.load()};
}

// ---- ArenaAllocator --------------------------------------------------------

ArenaAllocator::ArenaAllocator(std::size_t capacity, Allocator& backing)
    : capacity_(capacity), backing_(&backing) {
    base_ = static_cast<std::byte*>(backing.allocate(capacity, kDefaultAlignment));
    AETHER_ASSERT(base_ != nullptr || capacity == 0, "arena backing allocation failed");
}

ArenaAllocator::ArenaAllocator(void* buffer, std::size_t capacity) noexcept
    : base_(static_cast<std::byte*>(buffer)), capacity_(capacity) {}

ArenaAllocator::~ArenaAllocator() {
    if (backing_ && base_) backing_->deallocate(base_, capacity_, kDefaultAlignment);
}

void* ArenaAllocator::allocate(std::size_t size, std::size_t align) noexcept {
    AETHER_ASSERT(is_power_of_two(align), "alignment must be a power of two");
    if (!base_) return nullptr;
    const auto base_addr = reinterpret_cast<std::uintptr_t>(base_);
    const std::uintptr_t aligned = align_up(base_addr + offset_, align);
    const std::size_t start = static_cast<std::size_t>(aligned - base_addr);
    if (start > capacity_ || size > capacity_ - start) return nullptr;
    offset_ = start + size;
    if (offset_ > high_water_) high_water_ = offset_;
    return base_ + start;
}

void ArenaAllocator::rewind(Marker m) noexcept {
    AETHER_ASSERT(m <= offset_, "cannot rewind an arena forward");
    offset_ = m;
}

// ---- FrameAllocator --------------------------------------------------------

FrameAllocator::FrameAllocator(std::size_t bytes_per_frame, std::size_t frames_in_flight,
                               Allocator& backing) {
    AETHER_ASSERT(frames_in_flight >= 1, "need at least one frame arena");
    arenas_.reserve(frames_in_flight);
    for (std::size_t i = 0; i < frames_in_flight; ++i)
        arenas_.push_back(std::make_unique<ArenaAllocator>(bytes_per_frame, backing));
}

void FrameAllocator::begin_frame() noexcept {
    index_ = (index_ + 1) % arenas_.size();
    arenas_[index_]->reset();
    ++frame_;
}

// ---- PoolAllocator ---------------------------------------------------------

PoolAllocator::PoolAllocator(std::size_t block_size, std::size_t block_count, Allocator& backing,
                             std::size_t align)
    : block_count_(block_count), align_(align), backing_(backing) {
    AETHER_ASSERT(is_power_of_two(align), "alignment must be a power of two");
    if (align_ < alignof(FreeNode)) align_ = alignof(FreeNode);
    std::size_t size = block_size < sizeof(FreeNode) ? sizeof(FreeNode) : block_size;
    block_size_ = static_cast<std::size_t>(align_up(size, align_));

    base_ = static_cast<std::byte*>(backing_.allocate(block_size_ * block_count_, align_));
    AETHER_ASSERT(base_ != nullptr || block_count == 0, "pool backing allocation failed");
    for (std::size_t i = block_count_; i-- > 0;) {
        auto* node = reinterpret_cast<FreeNode*>(base_ + i * block_size_);
        node->next = free_list_;
        free_list_ = node;
    }
    free_count_ = block_count_;
}

PoolAllocator::~PoolAllocator() {
    if (base_) backing_.deallocate(base_, block_size_ * block_count_, align_);
}

void* PoolAllocator::allocate(std::size_t size, std::size_t align) noexcept {
    if (size > block_size_ || align > align_ || !free_list_) return nullptr;
    FreeNode* node = free_list_;
    free_list_ = node->next;
    --free_count_;
    return node;
}

void PoolAllocator::deallocate(void* ptr, std::size_t, std::size_t) noexcept {
    if (!ptr) return;
    AETHER_ASSERT(owns(ptr), "pointer does not belong to this pool");
    auto* node = static_cast<FreeNode*>(ptr);
    node->next = free_list_;
    free_list_ = node;
    ++free_count_;
}

bool PoolAllocator::owns(const void* ptr) const noexcept {
    const auto p = reinterpret_cast<std::uintptr_t>(ptr);
    const auto b = reinterpret_cast<std::uintptr_t>(base_);
    if (!base_ || p < b || p >= b + block_size_ * block_count_) return false;
    return (p - b) % block_size_ == 0;
}

}  // namespace aether
