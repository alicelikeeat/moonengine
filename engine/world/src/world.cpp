#include "aether/world/world.hpp"

#include <algorithm>

namespace aether::world {

World::World(WorldDesc desc) {
    if (desc.allocator) {
        allocator_ = desc.allocator;
    } else {
        owned_allocator_ = std::make_unique<HeapAllocator>();
        allocator_ = owned_allocator_.get();
    }
    std::uint64_t seed = desc.seed;
    if (seed == 0) {
        std::random_device rd;
        seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }
    rng_.seed(seed);
    empty_archetype_ = archetype_for({});
}

World::~World() {
    AETHER_ASSERT(iterating_.load() == 0, "World destroyed during iteration");
    // Archetypes destroy their components before the allocator goes away.
    archetypes_.clear();
}

// ---- Component types --------------------------------------------------------

ComponentId World::register_component(const void* key, ComponentInfo info) {
    assert_not_iterating();
    AETHER_ASSERT(infos_.size() < kInvalidComponent, "too many component types");
    const auto id = static_cast<ComponentId>(infos_.size());
    if (info.stable_name) {
        const bool inserted = ids_by_name_.emplace(info.name, id).second;
        AETHER_ASSERT(inserted, "two component types share the same kComponentName");
    }
    infos_.push_back(std::move(info));
    ids_by_type_.emplace(key, id);
    return id;
}

std::optional<ComponentId> World::find_component(std::string_view name) const {
    if (const auto it = ids_by_name_.find(std::string(name)); it != ids_by_name_.end()) return it->second;
    return std::nullopt;
}

const ComponentInfo& World::component_info(ComponentId id) const {
    AETHER_ASSERT(id < infos_.size(), "unknown component id");
    return infos_[id];
}

// ---- Archetypes -------------------------------------------------------------

detail::Archetype* World::archetype_for(std::vector<ComponentId> types) {
    if (const auto it = archetype_index_.find(types); it != archetype_index_.end()) return it->second;
    std::vector<const ComponentInfo*> infos;
    infos.reserve(types.size());
    for (const ComponentId id : types) infos.push_back(&infos_[id]);
    auto arch = std::make_unique<detail::Archetype>(types, std::move(infos), *allocator_);
    detail::Archetype* raw = arch.get();
    archetypes_.push_back(std::move(arch));
    archetype_index_.emplace(std::move(types), raw);
    return raw;
}

detail::Archetype* World::archetype_with(detail::Archetype* from, ComponentId added) {
    if (const auto it = from->add_edges.find(added); it != from->add_edges.end()) return it->second;
    std::vector<ComponentId> types = from->types();
    types.insert(std::lower_bound(types.begin(), types.end(), added), added);
    detail::Archetype* to = archetype_for(std::move(types));
    from->add_edges.emplace(added, to);
    to->remove_edges.emplace(added, from);
    return to;
}

detail::Archetype* World::archetype_without(detail::Archetype* from, ComponentId removed) {
    if (const auto it = from->remove_edges.find(removed); it != from->remove_edges.end()) return it->second;
    std::vector<ComponentId> types = from->types();
    types.erase(std::lower_bound(types.begin(), types.end(), removed));
    detail::Archetype* to = archetype_for(std::move(types));
    from->remove_edges.emplace(removed, to);
    to->add_edges.emplace(removed, from);
    return to;
}

std::uint32_t World::move_entity(Entity e, detail::Archetype* to) {
    Record& rec = records_[e.index];
    detail::Archetype* from = rec.archetype;
    const std::uint32_t old_row = rec.row;
    const std::uint32_t new_row = to->push(e);

    const auto& to_types = to->types();
    for (std::size_t c = 0; c < to_types.size(); ++c) {
        const int src = from->column_of(to_types[c]);
        if (src >= 0)
            infos_[to_types[c]].move_construct(to->at(c, new_row),
                                               from->at(static_cast<std::size_t>(src), old_row));
    }
    // Destroys the moved-from objects (and any component being removed).
    const Entity moved = from->swap_remove(old_row);
    if (!moved.is_null()) records_[moved.index].row = old_row;

    rec.archetype = to;
    rec.row = new_row;
    return new_row;
}

// ---- Entities ---------------------------------------------------------------

const World::Record* World::record_of(Entity e) const noexcept {
    if (e.index >= records_.size()) return nullptr;
    const Record& r = records_[e.index];
    if (!r.archetype || r.generation != e.generation) return nullptr;
    return &r;
}

bool World::alive(Entity e) const noexcept { return record_of(e) != nullptr; }

Entity World::create_internal(const Uuid& id) {
    assert_not_iterating();
    std::uint32_t index;
    if (!free_indices_.empty()) {
        index = free_indices_.back();
        free_indices_.pop_back();
    } else {
        AETHER_ASSERT(records_.size() < Entity::kInvalidIndex, "entity index space exhausted");
        index = static_cast<std::uint32_t>(records_.size());
        records_.emplace_back();
    }
    Record& rec = records_[index];
    const Entity e{index, rec.generation};
    rec.id = id;
    rec.archetype = empty_archetype_;
    rec.row = empty_archetype_->push(e);
    index_by_id_.emplace(id, index);
    ++alive_count_;
    return e;
}

Entity World::create() {
    Uuid id = Uuid::generate(rng_);
    while (index_by_id_.contains(id)) id = Uuid::generate(rng_);  // astronomically rare
    return create_internal(id);
}

Result<Entity> World::create_with_id(const Uuid& id) {
    if (id.is_nil()) return Error{ErrorCode::InvalidArgument, "entity id must not be nil"};
    if (index_by_id_.contains(id))
        return Error{ErrorCode::AlreadyExists, "an entity with id " + id.to_string() + " already exists"};
    return create_internal(id);
}

bool World::destroy(Entity e) {
    if (!alive(e)) return false;
    assert_not_iterating();
    Record& rec = records_[e.index];
    const Entity moved = rec.archetype->swap_remove(rec.row);
    if (!moved.is_null()) records_[moved.index].row = rec.row;
    index_by_id_.erase(rec.id);
    rec.archetype = nullptr;
    rec.id = Uuid{};
    ++rec.generation;  // wraps after 2^32 reuses of one slot
    free_indices_.push_back(e.index);
    --alive_count_;
    return true;
}

Uuid World::id_of(Entity e) const noexcept {
    const Record* r = record_of(e);
    return r ? r->id : Uuid{};
}

Entity World::find(const Uuid& id) const {
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) return kNullEntity;
    return Entity{it->second, records_[it->second].generation};
}

bool World::has(Entity e, ComponentId id) const noexcept {
    const Record* r = record_of(e);
    return r && r->archetype->column_of(id) >= 0;
}

void* World::get_raw(Entity e, ComponentId id) const noexcept {
    const Record* r = record_of(e);
    if (!r) return nullptr;
    const int col = r->archetype->column_of(id);
    return col < 0 ? nullptr : r->archetype->at(static_cast<std::size_t>(col), r->row);
}

std::vector<ComponentId> World::components_of(Entity e) const {
    const Record* r = record_of(e);
    return r ? r->archetype->types() : std::vector<ComponentId>{};
}

}  // namespace aether::world
