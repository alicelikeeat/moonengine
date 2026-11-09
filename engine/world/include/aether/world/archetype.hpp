// Archetype storage (internal to World, exposed only because World's
// templates need it).
//
// An archetype holds every entity that has exactly the same set of
// component types. Each component type is stored in its own contiguous
// column (structure of arrays), so systems iterate tightly packed memory.
// Removing an entity moves the last row into the hole (swap-remove).
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "aether/foundation/memory.hpp"
#include "aether/world/component.hpp"
#include "aether/world/entity.hpp"

namespace aether::world::detail {

class Archetype {
public:
    // `types` must be sorted and unique; `infos` must outlive the archetype.
    Archetype(std::vector<ComponentId> types, std::vector<const ComponentInfo*> infos,
              Allocator& allocator);
    ~Archetype();

    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;

    [[nodiscard]] const std::vector<ComponentId>& types() const noexcept { return types_; }
    [[nodiscard]] std::size_t size() const noexcept { return entities_.size(); }
    [[nodiscard]] Entity entity(std::size_t row) const noexcept { return entities_[row]; }

    // Column index of `id`, or -1.
    [[nodiscard]] int column_of(ComponentId id) const noexcept;
    [[nodiscard]] void* at(std::size_t column, std::size_t row) const noexcept {
        return columns_[column].data + row * columns_[column].info->size;
    }
    [[nodiscard]] std::byte* column_data(std::size_t column) const noexcept { return columns_[column].data; }

    // Appends a row for `e`. Component slots in the row are left
    // uninitialised; the caller must construct every one of them.
    std::uint32_t push(Entity e);

    // Destroys the components in `row` and fills the hole with the last row.
    // Returns the entity that moved into `row`, or kNullEntity if none did.
    Entity swap_remove(std::size_t row) noexcept;

    std::unordered_map<ComponentId, Archetype*> add_edges;
    std::unordered_map<ComponentId, Archetype*> remove_edges;

private:
    struct Column {
        const ComponentInfo* info;
        std::byte* data = nullptr;
    };

    void grow();

    std::vector<ComponentId> types_;
    std::vector<Column> columns_;
    std::vector<Entity> entities_;
    std::size_t capacity_ = 0;
    Allocator& allocator_;
};

}  // namespace aether::world::detail
