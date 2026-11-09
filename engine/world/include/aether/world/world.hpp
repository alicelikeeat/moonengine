// World: entities, components and queries.
//
//   World world;
//   Entity e = world.create();
//   world.add<Position>(e, Position{1, 2, 3});
//   world.each<const Velocity, Position>([](Entity, const Velocity& v, Position& p) { ... });
//
// Threading: queries (each, par_each, get, has) may run concurrently with
// each other. Structural changes (create, destroy, add of a new component,
// remove) must not overlap with anything else; inside queries and systems,
// record them in a CommandBuffer. Violations are caught by an assertion.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/foundation/assert.hpp"
#include "aether/foundation/job_system.hpp"
#include "aether/foundation/memory.hpp"
#include "aether/foundation/result.hpp"
#include "aether/foundation/uuid.hpp"
#include "aether/world/archetype.hpp"
#include "aether/world/component.hpp"
#include "aether/world/entity.hpp"

namespace aether::world {

struct WorldDesc {
    // Seed for entity UUID generation. 0 seeds from std::random_device;
    // any other value makes UUIDs reproducible (tests, replays).
    std::uint64_t seed = 0;
    // Backing memory for component storage. nullptr uses an internal heap.
    Allocator* allocator = nullptr;
};

class World {
public:
    explicit World(WorldDesc desc = {});
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---- Component types ----------------------------------------------

    // Returns the id for T, registering it on first use (structural).
    template <Component T>
    ComponentId component_id();
    // Returns the id for T if registered. Safe during queries.
    template <Component T>
    [[nodiscard]] std::optional<ComponentId> find_component_id() const;
    // Looks up a component by its kComponentName (for tools and ACP).
    [[nodiscard]] std::optional<ComponentId> find_component(std::string_view name) const;
    [[nodiscard]] const ComponentInfo& component_info(ComponentId id) const;
    [[nodiscard]] std::size_t component_type_count() const noexcept { return infos_.size(); }

    // ---- Entities -----------------------------------------------------

    Entity create();
    // Creates an entity with a known persistent id (e.g. when loading).
    Result<Entity> create_with_id(const Uuid& id);
    // Returns false if `e` was not alive.
    bool destroy(Entity e);

    [[nodiscard]] bool alive(Entity e) const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept { return alive_count_; }
    [[nodiscard]] Uuid id_of(Entity e) const noexcept;   // nil if not alive
    [[nodiscard]] Entity find(const Uuid& id) const;     // kNullEntity if absent

    // ---- Components on entities ---------------------------------------

    // Adds T constructed from args, or replaces the existing value.
    template <Component T, typename... Args>
    T& add(Entity e, Args&&... args);
    // Returns false if the entity did not have T.
    template <Component T>
    bool remove(Entity e);

    template <Component T>
    [[nodiscard]] T* get(Entity e) noexcept;
    template <Component T>
    [[nodiscard]] const T* get(Entity e) const noexcept;
    // Faster variants for hot loops: `id` must be component_id<T>(), resolved
    // once by the caller. Skips the type-to-id hash lookup.
    template <Component T>
    [[nodiscard]] T* get(Entity e, ComponentId id) noexcept {
        return static_cast<T*>(get_raw(e, id));
    }
    template <Component T>
    [[nodiscard]] const T* get(Entity e, ComponentId id) const noexcept {
        return static_cast<const T*>(get_raw(e, id));
    }
    [[nodiscard]] void* get_raw(Entity e, ComponentId id) const noexcept;

    template <Component T>
    [[nodiscard]] bool has(Entity e) const noexcept { return get<T>(e) != nullptr; }
    [[nodiscard]] bool has(Entity e, ComponentId id) const noexcept;
    // Sorted component ids of a live entity (empty if not alive).
    [[nodiscard]] std::vector<ComponentId> components_of(Entity e) const;

    // ---- Queries ------------------------------------------------------

    // Calls fn(Entity, Ts&...) or fn(Ts&...) for every entity having all Ts.
    // Use `const T` for read-only access.
    template <typename... Ts, typename Fn>
    void each(Fn&& fn);
    // As each(), but splits every matching archetype into chunks of `grain`
    // rows run on `jobs`. fn must be safe to call concurrently.
    template <typename... Ts, typename Fn>
    void par_each(JobSystem& jobs, std::size_t grain, Fn&& fn);
    template <typename... Ts>
    [[nodiscard]] std::size_t count() const;

    // Calls fn(Entity) for every live entity, in storage order.
    template <typename Fn>
    void for_each_entity(Fn&& fn) {
        IterationGuard guard(*this);
        for (const auto& arch : archetypes_)
            for (std::size_t row = 0; row < arch->size(); ++row) fn(arch->entity(row));
    }

    [[nodiscard]] std::size_t archetype_count() const noexcept { return archetypes_.size(); }

private:
    struct Record {
        detail::Archetype* archetype = nullptr;  // nullptr when the slot is free
        std::uint32_t row = 0;
        std::uint32_t generation = 0;
        Uuid id;
    };

    class IterationGuard {
    public:
        explicit IterationGuard(const World& w) noexcept : w_(w) { w_.iterating_.fetch_add(1, std::memory_order_acq_rel); }
        ~IterationGuard() { w_.iterating_.fetch_sub(1, std::memory_order_acq_rel); }
        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;
    private:
        const World& w_;
    };

    void assert_not_iterating() const noexcept {
        AETHER_ASSERT(iterating_.load(std::memory_order_acquire) == 0,
                      "structural change during iteration; record it in a CommandBuffer");
    }
    [[nodiscard]] const Record* record_of(Entity e) const noexcept;
    Entity create_internal(const Uuid& id);
    ComponentId register_component(const void* key, ComponentInfo info);
    detail::Archetype* archetype_for(std::vector<ComponentId> types);
    detail::Archetype* archetype_with(detail::Archetype* from, ComponentId added);
    detail::Archetype* archetype_without(detail::Archetype* from, ComponentId removed);
    // Moves `e` to `to`, keeping the components both archetypes share.
    // Returns the new row; slots `to` has but the old archetype lacks are
    // left uninitialised.
    std::uint32_t move_entity(Entity e, detail::Archetype* to);

    template <typename... Ts, std::size_t... I>
    [[nodiscard]] bool resolve_ids(std::array<ComponentId, sizeof...(Ts)>& ids,
                                   std::index_sequence<I...>) const;

    template <typename... Ts, typename Fn, std::size_t... I>
    static void run_rows(detail::Archetype& arch, const std::array<int, sizeof...(Ts)>& cols,
                         std::size_t begin, std::size_t end, Fn& fn, std::index_sequence<I...>);

    std::unique_ptr<HeapAllocator> owned_allocator_;
    Allocator* allocator_;
    std::mt19937_64 rng_;

    std::deque<ComponentInfo> infos_;  // deque: stable addresses for archetypes
    std::unordered_map<const void*, ComponentId> ids_by_type_;
    std::unordered_map<std::string, ComponentId> ids_by_name_;

    std::vector<std::unique_ptr<detail::Archetype>> archetypes_;
    std::map<std::vector<ComponentId>, detail::Archetype*> archetype_index_;
    detail::Archetype* empty_archetype_ = nullptr;

    std::vector<Record> records_;
    std::vector<std::uint32_t> free_indices_;
    std::unordered_map<Uuid, std::uint32_t, UuidHash> index_by_id_;
    std::size_t alive_count_ = 0;

    mutable std::atomic<int> iterating_{0};
};

// ---- Template implementation ------------------------------------------------

template <Component T>
ComponentId World::component_id() {
    if (const auto it = ids_by_type_.find(detail::type_key<T>()); it != ids_by_type_.end())
        return it->second;
    return register_component(detail::type_key<T>(), make_component_info<T>());
}

template <Component T>
std::optional<ComponentId> World::find_component_id() const {
    if (const auto it = ids_by_type_.find(detail::type_key<T>()); it != ids_by_type_.end())
        return it->second;
    return std::nullopt;
}

template <Component T, typename... Args>
T& World::add(Entity e, Args&&... args) {
    AETHER_ASSERT(alive(e), "World::add on a dead entity");
    // Build the value first: if the constructor throws, the world is unchanged.
    T value(std::forward<Args>(args)...);
    if (T* existing = get<T>(e)) {
        existing->~T();
        return *::new (static_cast<void*>(existing)) T(std::move(value));
    }
    assert_not_iterating();
    const ComponentId id = component_id<T>();
    detail::Archetype* to = archetype_with(records_[e.index].archetype, id);
    const std::uint32_t row = move_entity(e, to);
    void* slot = to->at(static_cast<std::size_t>(to->column_of(id)), row);
    return *::new (slot) T(std::move(value));
}

template <Component T>
bool World::remove(Entity e) {
    if (!has<T>(e)) return false;
    assert_not_iterating();
    const ComponentId id = *find_component_id<T>();
    move_entity(e, archetype_without(records_[e.index].archetype, id));
    return true;
}

template <Component T>
T* World::get(Entity e) noexcept {
    return const_cast<T*>(std::as_const(*this).get<T>(e));
}

template <Component T>
const T* World::get(Entity e) const noexcept {
    const Record* r = record_of(e);
    if (!r) return nullptr;
    const auto id = find_component_id<T>();
    if (!id) return nullptr;
    const int col = r->archetype->column_of(*id);
    if (col < 0) return nullptr;
    return static_cast<const T*>(r->archetype->at(static_cast<std::size_t>(col), r->row));
}

template <typename... Ts, std::size_t... I>
bool World::resolve_ids(std::array<ComponentId, sizeof...(Ts)>& ids, std::index_sequence<I...>) const {
    bool all = true;
    (([&] {
         const auto id = find_component_id<std::remove_const_t<Ts>>();
         if (id) ids[I] = *id; else all = false;
     }()), ...);
    return all;
}

template <typename... Ts, typename Fn, std::size_t... I>
void World::run_rows(detail::Archetype& arch, const std::array<int, sizeof...(Ts)>& cols,
                     std::size_t begin, std::size_t end, Fn& fn, std::index_sequence<I...>) {
    std::tuple<Ts*...> base{reinterpret_cast<Ts*>(arch.column_data(static_cast<std::size_t>(cols[I])))...};
    for (std::size_t row = begin; row < end; ++row) {
        if constexpr (std::is_invocable_v<Fn&, Entity, Ts&...>)
            fn(arch.entity(row), std::get<I>(base)[row]...);
        else
            fn(std::get<I>(base)[row]...);
    }
}

template <typename... Ts, typename Fn>
void World::each(Fn&& fn) {
    static_assert(sizeof...(Ts) > 0, "each<> needs at least one component type");
    static_assert((Component<std::remove_const_t<Ts>> && ...), "each<> arguments must be components");
    static_assert(std::is_invocable_v<Fn&, Entity, Ts&...> || std::is_invocable_v<Fn&, Ts&...>,
                  "callback must accept (Entity, Ts&...) or (Ts&...)");
    std::array<ComponentId, sizeof...(Ts)> ids{};
    if (!resolve_ids<Ts...>(ids, std::index_sequence_for<Ts...>{})) return;

    IterationGuard guard(*this);
    for (const auto& arch : archetypes_) {
        if (arch->size() == 0) continue;
        std::array<int, sizeof...(Ts)> cols{};
        bool match = true;
        for (std::size_t i = 0; i < ids.size() && match; ++i) {
            cols[i] = arch->column_of(ids[i]);
            match = cols[i] >= 0;
        }
        if (match) run_rows<Ts...>(*arch, cols, 0, arch->size(), fn, std::index_sequence_for<Ts...>{});
    }
}

template <typename... Ts, typename Fn>
void World::par_each(JobSystem& jobs, std::size_t grain, Fn&& fn) {
    static_assert(sizeof...(Ts) > 0, "par_each<> needs at least one component type");
    static_assert(std::is_invocable_v<Fn&, Entity, Ts&...> || std::is_invocable_v<Fn&, Ts&...>,
                  "callback must accept (Entity, Ts&...) or (Ts&...)");
    if (grain == 0) grain = 1;
    std::array<ComponentId, sizeof...(Ts)> ids{};
    if (!resolve_ids<Ts...>(ids, std::index_sequence_for<Ts...>{})) return;

    IterationGuard guard(*this);
    WaitGroup group;
    for (const auto& arch : archetypes_) {
        const std::size_t n = arch->size();
        if (n == 0) continue;
        std::array<int, sizeof...(Ts)> cols{};
        bool match = true;
        for (std::size_t i = 0; i < ids.size() && match; ++i) {
            cols[i] = arch->column_of(ids[i]);
            match = cols[i] >= 0;
        }
        if (!match) continue;
        detail::Archetype* a = arch.get();
        for (std::size_t begin = 0; begin < n; begin += grain) {
            const std::size_t end = (n - begin > grain) ? begin + grain : n;
            jobs.submit([a, cols, begin, end, &fn] {
                run_rows<Ts...>(*a, cols, begin, end, fn, std::index_sequence_for<Ts...>{});
            }, &group);
        }
    }
    jobs.wait(group);
}

template <typename... Ts>
std::size_t World::count() const {
    std::array<ComponentId, sizeof...(Ts)> ids{};
    if (!resolve_ids<Ts...>(ids, std::index_sequence_for<Ts...>{})) return 0;
    std::size_t total = 0;
    for (const auto& arch : archetypes_) {
        bool match = true;
        for (const ComponentId id : ids) match = match && arch->column_of(id) >= 0;
        if (match) total += arch->size();
    }
    return total;
}

}  // namespace aether::world
