// System scheduling.
//
// Each system declares which component types it reads and writes. The
// schedule runs systems in registration order, except that systems whose
// accesses do not conflict run in parallel on the JobSystem. Two systems
// conflict if either writes a component the other reads or writes, or if
// either is exclusive. The plan is deterministic: the same systems always
// produce the same waves.
//
// Systems only reach the world through SystemContext, which checks every
// query against the declared access, so a missing declaration fails loudly
// instead of becoming a data race. Structural changes go through
// ctx.commands; all command buffers are applied after the last wave, in
// system registration order.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/foundation/assert.hpp"
#include "aether/foundation/job_system.hpp"
#include "aether/world/command_buffer.hpp"
#include "aether/world/world.hpp"

namespace aether::world {

class SystemAccess {
public:
    // Access for the component list of a query: `const T` reads, `T` writes.
    template <typename... Ts>
    static SystemAccess of(World& world) {
        SystemAccess a;
        (a.add_one<Ts>(world), ...);
        return a;
    }
    // Conflicts with every other system; may use ctx.world() directly.
    static SystemAccess exclusive() {
        SystemAccess a;
        a.exclusive_ = true;
        return a;
    }

    [[nodiscard]] bool reads(ComponentId id) const noexcept;
    [[nodiscard]] bool writes(ComponentId id) const noexcept;
    [[nodiscard]] bool is_exclusive() const noexcept { return exclusive_; }
    [[nodiscard]] bool conflicts_with(const SystemAccess& other) const noexcept;

private:
    template <typename T>
    void add_one(World& world) {
        const ComponentId id = world.component_id<std::remove_const_t<T>>();
        if constexpr (std::is_const_v<T>) insert(reads_, id); else insert(writes_, id);
    }
    static void insert(std::vector<ComponentId>& set, ComponentId id);

    std::vector<ComponentId> reads_;   // sorted
    std::vector<ComponentId> writes_;  // sorted
    bool exclusive_ = false;
};

class SystemContext {
public:
    SystemContext(World& world, const SystemAccess& access, CommandBuffer& command_buffer,
                  double delta_seconds) noexcept
        : world_(world), access_(access), commands(command_buffer), dt(delta_seconds) {}

    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        (check<Ts>(), ...);
        world_.each<Ts...>(std::forward<Fn>(fn));
    }

    template <typename... Ts, typename Fn>
    void par_each(JobSystem& jobs, std::size_t grain, Fn&& fn) {
        (check<Ts>(), ...);
        world_.par_each<Ts...>(jobs, grain, std::forward<Fn>(fn));
    }

    template <typename T>
    [[nodiscard]] auto* get(Entity e) {
        check<T>();
        if constexpr (std::is_const_v<T>) return std::as_const(world_).get<std::remove_const_t<T>>(e);
        else return world_.get<T>(e);
    }

    [[nodiscard]] bool alive(Entity e) const noexcept { return world_.alive(e); }

    // Only for systems declared with SystemAccess::exclusive().
    [[nodiscard]] World& world() {
        AETHER_ASSERT(access_.is_exclusive(), "ctx.world() requires SystemAccess::exclusive()");
        return world_;
    }

private:
    template <typename T>
    void check() const {
        if (access_.is_exclusive()) return;
        const auto id = world_.find_component_id<std::remove_const_t<T>>();
        if (!id) return;  // never registered, so no entity can have it
        if constexpr (std::is_const_v<T>)
            AETHER_ASSERT(access_.reads(*id) || access_.writes(*id), "system reads a component it did not declare");
        else
            AETHER_ASSERT(access_.writes(*id), "system writes a component it did not declare (use const T to read)");
    }

    World& world_;
    const SystemAccess& access_;

public:
    CommandBuffer& commands;
    const double dt;
};

class Schedule {
public:
    using SystemFn = std::function<void(SystemContext&)>;

    void add_system(std::string name, SystemAccess access, SystemFn fn);

    // Groups system names into waves; systems within a wave run concurrently.
    [[nodiscard]] std::vector<std::vector<std::string>> plan() const;

    // Runs every system once. With jobs == nullptr everything runs on the
    // calling thread in registration order.
    void run(World& world, JobSystem* jobs, double dt);

    [[nodiscard]] std::size_t system_count() const noexcept { return systems_.size(); }

private:
    struct System {
        std::string name;
        SystemAccess access;
        SystemFn fn;
    };

    void rebuild_waves() const;

    std::vector<System> systems_;
    mutable std::vector<std::vector<std::size_t>> waves_;
    mutable bool dirty_ = true;
};

}  // namespace aether::world
