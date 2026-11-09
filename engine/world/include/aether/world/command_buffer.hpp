// Deferred structural changes.
//
// Systems and queries must not create, destroy, or change the component
// set of entities while iterating. They record those changes here and the
// buffer is applied later, in recording order, on a single thread.
//
// Commands that target an entity that is no longer alive when the buffer
// is applied are skipped and counted, not treated as errors: another
// command may legitimately have destroyed it first.
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "aether/world/world.hpp"

namespace aether::world {

class CommandBuffer {
public:
    struct ApplyStats {
        std::size_t applied = 0;
        std::size_t skipped = 0;
    };

    void destroy(Entity e) { push<DestroyCmd>(e); }

    template <Component T>
    void add(Entity e, T value) { push<AddCmd<T>>(e, std::move(value)); }

    template <Component T>
    void remove(Entity e) { push<RemoveCmd<T>>(e); }

    // Creates an entity when applied, then calls init(world, entity).
    template <typename Init>
    void spawn(Init init) { push<SpawnCmd<Init>>(std::move(init)); }
    void spawn() { spawn([](World&, Entity) {}); }

    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    void clear() noexcept { commands_.clear(); }

    // Applies and clears every command. Must not overlap any query.
    ApplyStats apply(World& world) {
        ApplyStats stats;
        for (auto& cmd : commands_) {
            if (cmd->apply(world)) ++stats.applied; else ++stats.skipped;
        }
        commands_.clear();
        return stats;
    }

private:
    struct Command {
        virtual ~Command() = default;
        virtual bool apply(World& world) = 0;  // false = skipped
    };

    struct DestroyCmd final : Command {
        explicit DestroyCmd(Entity target) : e(target) {}
        bool apply(World& w) override { return w.destroy(e); }
        Entity e;
    };

    template <Component T>
    struct AddCmd final : Command {
        AddCmd(Entity target, T v) : e(target), value(std::move(v)) {}
        bool apply(World& w) override {
            if (!w.alive(e)) return false;
            w.add<T>(e, std::move(value));
            return true;
        }
        Entity e;
        T value;
    };

    template <Component T>
    struct RemoveCmd final : Command {
        explicit RemoveCmd(Entity target) : e(target) {}
        bool apply(World& w) override { return w.remove<T>(e); }
        Entity e;
    };

    template <typename Init>
    struct SpawnCmd final : Command {
        explicit SpawnCmd(Init f) : init(std::move(f)) {}
        bool apply(World& w) override {
            init(w, w.create());
            return true;
        }
        Init init;
    };

    template <typename C, typename... Args>
    void push(Args&&... args) {
        commands_.push_back(std::make_unique<C>(std::forward<Args>(args)...));
    }

    std::vector<std::unique_ptr<Command>> commands_;
};

}  // namespace aether::world
