#include <string>

#include "aether/editor/snapshot.hpp"
#include "aether/editor/undo.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::editor;
using namespace aether::math;
namespace h = aether::world::hierarchy;

namespace {
world::Entity make(world::World& w, const char* name, Vec3 pos) {
    const world::Entity e = w.create();
    w.add<world::Name>(e, world::Name{name});
    w.add<world::Transform>(e, world::Transform{pos});
    return e;
}

// root -> {a -> {a1}, b}
struct Tree {
    world::Entity root, a, a1, b;
};
Tree make_tree(world::World& w) {
    Tree t{make(w, "root", {0, 0, 0}), make(w, "a", {1, 0, 0}), make(w, "a1", {2, 0, 0}), make(w, "b", {3, 0, 0})};
    w.add<render::MeshInstance>(t.a1, render::MeshInstance{0, {0.1f, 0.2f, 0.3f}});
    w.add<Spin>(t.b, Spin{{1, 0, 0}, 2.0f});
    (void)h::set_parent(w, t.a, t.root);
    (void)h::set_parent(w, t.b, t.root);
    (void)h::set_parent(w, t.a1, t.a);
    return t;
}
}  // namespace

AETHER_TEST(subtree_snapshot_is_parent_first_and_ordered) {
    world::World w({.seed = 1});
    const Tree t = make_tree(w);
    const auto states = capture_subtree(w, t.root);
    REQUIRE(states.size() == 4);
    CHECK(states[0].id == w.id_of(t.root));
    CHECK(states[1].id == w.id_of(t.a));
    CHECK(states[2].id == w.id_of(t.a1));
    CHECK(states[3].id == w.id_of(t.b));
    CHECK(states[3].child_index == 1u);
    CHECK(states[2].mesh.has_value());
    CHECK(states[3].spin.has_value());
}

AETHER_TEST(delete_and_restore_keeps_ids_links_and_order) {
    world::World w({.seed = 1});
    const Tree t = make_tree(w);
    const Uuid root_id = w.id_of(t.root), a_id = w.id_of(t.a), b_id = w.id_of(t.b), a1_id = w.id_of(t.a1);
    const auto before = capture_world(w);
    const auto states = capture_subtree(w, t.a);
    h::destroy_recursive(w, t.a);
    CHECK(w.find(a_id).is_null() && w.find(a1_id).is_null());

    restore(w, states);
    const world::Entity a = w.find(a_id), root = w.find(root_id), b = w.find(b_id);
    REQUIRE(!a.is_null());
    CHECK(h::parent_of(w, a) == root);
    const auto kids = h::children_of(w, root);
    REQUIRE(kids.size() == 2);
    CHECK(kids[0] == a);  // back in its original position, before b
    CHECK(kids[1] == b);
    CHECK(w.get<render::MeshInstance>(w.find(a1_id))->color.z == 0.3f);
    CHECK(h::validate(w).empty());
    const auto after = capture_world(w);
    CHECK_EQ(after.size(), before.size());
}

AETHER_TEST(undo_redo_edit_create_delete_reparent) {
    world::World w({.seed = 1});
    UndoStack undo;
    const Tree t = make_tree(w);
    const Uuid b_id = w.id_of(t.b), a_id = w.id_of(t.a);

    // Edit
    const EntityState before = capture(w, t.b);
    w.get<world::Transform>(t.b)->position = {9, 9, 9};
    w.add<world::Name>(t.b, world::Name{"renamed"});
    undo.push(make_edit_command("edit b", before, capture(w, t.b)));
    CHECK(undo.undo(w));
    CHECK(w.get<world::Transform>(w.find(b_id))->position.x == 3.0f);
    CHECK_EQ(w.get<world::Name>(w.find(b_id))->value, std::string("b"));
    CHECK(undo.redo(w));
    CHECK(w.get<world::Transform>(w.find(b_id))->position.x == 9.0f);

    // Create
    const world::Entity c = make(w, "c", {5, 0, 0});
    const Uuid c_id = w.id_of(c);
    undo.push(make_create_command("create c", capture_subtree(w, c)));
    CHECK(undo.undo(w));
    CHECK(w.find(c_id).is_null());
    CHECK(undo.redo(w));
    CHECK(!w.find(c_id).is_null());

    // Delete a subtree
    const auto deleted = capture_subtree(w, w.find(a_id));
    h::destroy_recursive(w, w.find(a_id));
    undo.push(make_delete_command("delete a", deleted));
    CHECK(undo.undo(w));
    CHECK(!w.find(a_id).is_null());
    CHECK_EQ(h::children_of(w, w.find(a_id)).size(), std::size_t{1});
    CHECK(undo.redo(w));
    CHECK(w.find(a_id).is_null());
    CHECK(undo.undo(w));

    // Reparent b under c
    const EntityState rb = capture(w, w.find(b_id));
    REQUIRE(h::set_parent(w, w.find(b_id), w.find(c_id)).has_value());
    undo.push(make_reparent_command("attach b", rb, capture(w, w.find(b_id))));
    CHECK(undo.undo(w));
    CHECK(h::parent_of(w, w.find(b_id)) == w.find(w.id_of(t.root)));
    CHECK(undo.redo(w));
    CHECK(h::parent_of(w, w.find(b_id)) == w.find(c_id));

    CHECK(h::validate(w).empty());
    CHECK_EQ(undo.undo_label(), std::string("attach b"));
}

AETHER_TEST(new_command_clears_redo_and_limit_is_enforced) {
    world::World w({.seed = 1});
    UndoStack undo(3);
    for (int i = 0; i < 5; ++i) undo.push({"step " + std::to_string(i), [](world::World&) {}, [](world::World&) {}});
    CHECK_EQ(undo.size(), std::size_t{3});
    CHECK(undo.undo(w));
    CHECK(undo.can_redo());
    undo.push({"new", [](world::World&) {}, [](world::World&) {}});
    CHECK(!undo.can_redo());
}
