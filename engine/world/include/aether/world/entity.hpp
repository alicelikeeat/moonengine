// Runtime entity handle.
//
// An Entity is an index plus a generation counter. When an entity is
// destroyed its index is recycled with a new generation, so stale handles
// are detected instead of silently aliasing a new entity. Handles are only
// valid inside one World for one run; persistent identity is the entity's
// Uuid (World::id_of / World::find).
#pragma once

#include <cstdint>
#include <functional>

namespace aether::world {

struct Entity {
    static constexpr std::uint32_t kInvalidIndex = 0xFFFF'FFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool is_null() const noexcept { return index == kInvalidIndex; }
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept {
        return (static_cast<std::uint64_t>(generation) << 32) | index;
    }
    friend constexpr bool operator==(Entity, Entity) = default;
};

inline constexpr Entity kNullEntity{};

}  // namespace aether::world

template <>
struct std::hash<aether::world::Entity> {
    std::size_t operator()(aether::world::Entity e) const noexcept {
        return std::hash<std::uint64_t>{}(e.bits());
    }
};
