#include "aether/editor/undo.hpp"

#include <utility>

#include "aether/world/hierarchy.hpp"

namespace aether::editor {

void UndoStack::push(Command command) {
    undone_.clear();
    done_.push_back(std::move(command));
    if (done_.size() > limit_) done_.erase(done_.begin());
}

bool UndoStack::undo(world::World& world) {
    if (done_.empty()) return false;
    Command c = std::move(done_.back());
    done_.pop_back();
    c.undo(world);
    undone_.push_back(std::move(c));
    return true;
}

bool UndoStack::redo(world::World& world) {
    if (undone_.empty()) return false;
    Command c = std::move(undone_.back());
    undone_.pop_back();
    c.redo(world);
    done_.push_back(std::move(c));
    return true;
}

const std::string& UndoStack::undo_label() const {
    static const std::string kEmpty;
    return done_.empty() ? kEmpty : done_.back().label;
}

const std::string& UndoStack::redo_label() const {
    static const std::string kEmpty;
    return undone_.empty() ? kEmpty : undone_.back().label;
}

namespace {
void apply_if_alive(world::World& w, const EntityState& s) {
    const world::Entity e = w.find(s.id);
    if (!e.is_null()) apply_components(w, e, s);
}

void delete_states(world::World& w, const std::vector<EntityState>& states) {
    if (states.empty()) return;
    const world::Entity root = w.find(states.front().id);
    if (!root.is_null()) world::hierarchy::destroy_recursive(w, root);
}
}  // namespace

Command make_edit_command(std::string label, EntityState before, EntityState after) {
    return {std::move(label), [before](world::World& w) { apply_if_alive(w, before); },
            [after](world::World& w) { apply_if_alive(w, after); }};
}

Command make_create_command(std::string label, std::vector<EntityState> created) {
    return {std::move(label), [created](world::World& w) { delete_states(w, created); },
            [created](world::World& w) { restore(w, created); }};
}

Command make_delete_command(std::string label, std::vector<EntityState> deleted) {
    return {std::move(label), [deleted](world::World& w) { restore(w, deleted); },
            [deleted](world::World& w) { delete_states(w, deleted); }};
}

Command make_reparent_command(std::string label, EntityState before, EntityState after) {
    // restore() updates components and re-links the single entity.
    return {std::move(label), [before](world::World& w) { restore(w, {before}); },
            [after](world::World& w) { restore(w, {after}); }};
}

}  // namespace aether::editor
