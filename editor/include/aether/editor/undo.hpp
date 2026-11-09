// Undo/redo stack.
//
// Each command stores how to undo and redo itself in terms of Uuids and
// snapshots, never runtime Entity handles, so commands stay valid after
// entities are deleted and recreated by other commands.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aether/editor/snapshot.hpp"
#include "aether/world/world.hpp"

namespace aether::editor {

struct Command {
    std::string label;
    std::function<void(world::World&)> undo;
    std::function<void(world::World&)> redo;
};

class UndoStack {
public:
    explicit UndoStack(std::size_t limit = 500) : limit_(limit) {}

    // Records an action that has already been applied.
    void push(Command command);
    bool undo(world::World& world);
    bool redo(world::World& world);
    void clear() noexcept {
        done_.clear();
        undone_.clear();
        ++revision_;
    }
    // Changes on every push, undo, redo and clear: compare to detect unsaved edits.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] bool can_undo() const noexcept { return !done_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !undone_.empty(); }
    [[nodiscard]] const std::string& undo_label() const;
    [[nodiscard]] const std::string& redo_label() const;
    [[nodiscard]] std::size_t size() const noexcept { return done_.size(); }

private:
    std::vector<Command> done_;
    std::vector<Command> undone_;
    std::size_t limit_;
    std::uint64_t revision_ = 0;
};

// Command factories for common editor actions.
// Property change on one entity: before/after component states.
[[nodiscard]] Command make_edit_command(std::string label, EntityState before, EntityState after);
// Entities created (already in the world): undo deletes them, redo restores.
[[nodiscard]] Command make_create_command(std::string label, std::vector<EntityState> created);
// Subtree deleted (captured before deleting): undo restores, redo deletes.
[[nodiscard]] Command make_delete_command(std::string label, std::vector<EntityState> deleted);
// Hierarchy move: entity `child` moved from `old_parent` to `new_parent`
// (nil Uuid = root), with its world position preserved by local transforms.
[[nodiscard]] Command make_reparent_command(std::string label, EntityState before, EntityState after);

}  // namespace aether::editor
