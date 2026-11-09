#include "aether/world/archetype.hpp"

#include <algorithm>

#include "aether/foundation/assert.hpp"

namespace aether::world::detail {

Archetype::Archetype(std::vector<ComponentId> types, std::vector<const ComponentInfo*> infos,
                     Allocator& allocator)
    : types_(std::move(types)), allocator_(allocator) {
    AETHER_ASSERT(types_.size() == infos.size(), "archetype types/infos mismatch");
    columns_.reserve(infos.size());
    for (const ComponentInfo* info : infos) columns_.push_back(Column{info, nullptr});
}

Archetype::~Archetype() {
    for (auto& col : columns_) {
        for (std::size_t row = 0; row < entities_.size(); ++row)
            col.info->destroy(col.data + row * col.info->size);
        if (col.data) allocator_.deallocate(col.data, col.info->size * capacity_, col.info->align);
    }
}

int Archetype::column_of(ComponentId id) const noexcept {
    const auto it = std::lower_bound(types_.begin(), types_.end(), id);
    if (it == types_.end() || *it != id) return -1;
    return static_cast<int>(it - types_.begin());
}

void Archetype::grow() {
    const std::size_t new_capacity = capacity_ == 0 ? 16 : capacity_ * 2;
    for (auto& col : columns_) {
        const std::size_t size = col.info->size;
        auto* fresh = static_cast<std::byte*>(allocator_.allocate(size * new_capacity, col.info->align));
        AETHER_ASSERT(fresh != nullptr, "out of memory growing an archetype column");
        for (std::size_t row = 0; row < entities_.size(); ++row) {
            col.info->move_construct(fresh + row * size, col.data + row * size);
            col.info->destroy(col.data + row * size);
        }
        if (col.data) allocator_.deallocate(col.data, size * capacity_, col.info->align);
        col.data = fresh;
    }
    capacity_ = new_capacity;
}

std::uint32_t Archetype::push(Entity e) {
    if (entities_.size() == capacity_) grow();
    entities_.push_back(e);
    return static_cast<std::uint32_t>(entities_.size() - 1);
}

Entity Archetype::swap_remove(std::size_t row) noexcept {
    const std::size_t last = entities_.size() - 1;
    for (auto& col : columns_) {
        const std::size_t size = col.info->size;
        col.info->destroy(col.data + row * size);
        if (row != last) {
            col.info->move_construct(col.data + row * size, col.data + last * size);
            col.info->destroy(col.data + last * size);
        }
    }
    Entity moved = kNullEntity;
    if (row != last) {
        entities_[row] = entities_[last];
        moved = entities_[row];
    }
    entities_.pop_back();
    return moved;
}

}  // namespace aether::world::detail
