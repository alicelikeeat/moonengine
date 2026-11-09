#include <cstdio>
#include <filesystem>
#include <random>
#include <vector>
#include <string>

#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"
#include "aether/world/serialization.hpp"
#include "aether_test.hpp"
#include "components.hpp"

using namespace aether;
using namespace aether::world;
using math::Quat;
using math::Vec3;
namespace h = aether::world::hierarchy;
using LevelName = aether::world::Name;  // tests/components.hpp also defines a Name

namespace {

SerializerRegistry registry_with_health() {
    SerializerRegistry r;
    register_core_components(r);
    r.add<Health>([](const Health& hp) { Json j = Json::object(); j.set("value", hp.value); return j; },
                  [](const Json& j) -> Result<Health> {
                      auto v = json_io::read_number(j, "value", 100);
                      if (!v) return v.error();
                      return Health{static_cast<int>(v.value())};
                  });
    return r;
}

Entity make(World& w, const char* name, Vec3 pos) {
    const Entity e = w.create();
    w.add<LevelName>(e, LevelName{name});
    w.add<Transform>(e, Transform{pos, math::normalize(Quat::from_axis_angle({0, 1, 0}, 0.3f)), {1, 2, 3}});
    return e;
}

}  // namespace

AETHER_TEST(level_round_trip_preserves_everything) {
    const auto reg = registry_with_health();
    World a({.seed = 11});
    const Entity root = make(a, "root", {1, 2, 3});
    const Entity c1 = make(a, "first", {0.1f, 0, 0});
    const Entity c2 = make(a, "second", {0, 0.2f, 0});
    const Entity bare = a.create();  // no components at all
    a.add<Health>(c2, Health{42});
    REQUIRE(h::set_parent(a, c1, root).has_value());
    REQUIRE(h::set_parent(a, c2, root).has_value());

    const std::string text = to_json_string(save_level(a, reg, "test"), 2);
    auto doc = parse_json(text);
    REQUIRE(doc.has_value());
    World b({.seed = 22});
    auto report = load_level(doc.value(), b, reg);
    REQUIRE(report.has_value());
    CHECK_EQ(report.value().entities, std::size_t{4});
    CHECK(report.value().warnings.empty());

    const Entity r2 = b.find(a.id_of(root));
    REQUIRE(!r2.is_null());
    CHECK_EQ(b.get<LevelName>(r2)->value, std::string("root"));
    CHECK(b.get<Transform>(r2)->position == (Vec3{1, 2, 3}));
    CHECK(b.get<Transform>(r2)->scale == (Vec3{1, 2, 3}));
    const auto kids = h::children_of(b, r2);
    REQUIRE(kids.size() == 2);
    CHECK(b.id_of(kids[0]) == a.id_of(c1));  // child order kept
    CHECK(b.id_of(kids[1]) == a.id_of(c2));
    CHECK_EQ(b.get<Health>(kids[1])->value, 42);
    CHECK(!b.find(a.id_of(bare)).is_null());
    CHECK(b.has<GlobalTransform>(kids[0]));  // transforms propagated on load

    // Saving the loaded world again gives the identical document.
    CHECK_EQ(to_json_string(save_level(b, reg, "test"), 2), text);
}

AETHER_TEST(level_float_values_round_trip_exactly) {
    SerializerRegistry reg;
    register_core_components(reg);
    World a({.seed = 3});
    const Entity e = a.create();
    a.add<Transform>(e, Transform{{0.1f, 1e-7f, 123456.789f}, Quat::identity(), {0.3f, 3.0f, 1.0f / 3.0f}});
    World b({.seed = 4});
    REQUIRE(load_level(parse_json(to_json_string(save_level(a, reg))).value(), b, reg).has_value());
    const Transform* t = b.get<Transform>(b.find(a.id_of(e)));
    REQUIRE(t != nullptr);
    CHECK(t->position == a.get<Transform>(e)->position);
    CHECK(t->scale == a.get<Transform>(e)->scale);
}

AETHER_TEST(level_unknown_and_invalid_components_are_kept) {
    SerializerRegistry reg;
    register_core_components(reg);
    auto doc = parse_json(R"({
      "format": "aether.level", "version": 1,
      "entities": [{
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "components": {
          "Name": {"value": "keeper"},
          "FutureThing": {"power": 9000, "tags": ["a", "b"]},
          "Transform": {"position": [1, 2]}
        }
      }]
    })");
    REQUIRE(doc.has_value());
    World w({.seed = 1});
    auto report = load_level(doc.value(), w, reg);
    REQUIRE(report.has_value());
    CHECK_EQ(report.value().warnings.size(), std::size_t{2});  // unknown type, bad Transform
    const Entity e = w.find(Uuid::parse("123e4567-e89b-42d3-a456-426614174000").value());
    REQUIRE(!e.is_null());
    CHECK(w.has<LevelName>(e));
    CHECK(!w.has<Transform>(e));
    REQUIRE(w.has<UnknownComponents>(e));

    const Json saved = save_level(w, reg);
    const Json& comps = *saved.find("entities")->as_array()[0].find("components");
    CHECK(comps.find("FutureThing")->find("power")->as_number() == 9000.0);
    CHECK(comps.find("Transform")->find("position")->as_array().size() == 2);  // original bytes kept
}

AETHER_TEST(level_load_rejects_bad_documents_without_side_effects) {
    SerializerRegistry reg;
    register_core_components(reg);
    const char* bad[] = {
        R"([])",
        R"({"format": "something.else", "version": 1, "entities": []})",
        R"({"format": "aether.level", "version": 1.5, "entities": []})",
        R"({"format": "aether.level", "version": 0, "entities": []})",
        R"({"format": "aether.level", "version": 1})",
        R"({"format": "aether.level", "version": 1, "entities": [{"id": "nope"}]})",
        R"({"format": "aether.level", "version": 1, "entities": [{"id": "123e4567-e89b-42d3-a456-426614174000"},
                                                                {"id": "123e4567-e89b-42d3-a456-426614174000"}]})",
        R"({"format": "aether.level", "version": 1, "entities": [{"id": "123e4567-e89b-42d3-a456-426614174000", "components": []}]})",
    };
    for (const char* text : bad) {
        World w({.seed = 1});
        auto doc = parse_json(text);
        REQUIRE(doc.has_value());
        CHECK(!load_level(doc.value(), w, reg).has_value());
        CHECK_EQ(w.entity_count(), std::size_t{0});
    }
    World w({.seed = 1});
    auto newer = load_level(parse_json(R"({"format": "aether.level", "version": 99, "entities": []})").value(), w, reg);
    REQUIRE(!newer);
    CHECK(newer.error().code == ErrorCode::Unsupported);
    // Existing ids in the target world are a conflict, and nothing is added.
    (void)w.create();  // an entity that the saved document will collide with
    Json doc = save_level(w, reg);
    auto conflict = load_level(doc, w, reg);
    REQUIRE(!conflict);
    CHECK(conflict.error().code == ErrorCode::AlreadyExists);
    CHECK_EQ(w.entity_count(), std::size_t{1});
}

AETHER_TEST(level_missing_parent_and_cycles_become_roots) {
    SerializerRegistry reg;
    register_core_components(reg);
    auto doc = parse_json(R"({"format": "aether.level", "version": 1, "entities": [
        {"id": "00000000-0000-4000-8000-000000000001", "parent": "00000000-0000-4000-8000-000000000002"},
        {"id": "00000000-0000-4000-8000-000000000002", "parent": "00000000-0000-4000-8000-000000000001"},
        {"id": "00000000-0000-4000-8000-000000000003", "parent": "00000000-0000-4000-8000-0000000000ff"}
    ]})");
    REQUIRE(doc.has_value());
    World w({.seed = 1});
    auto report = load_level(doc.value(), w, reg);
    REQUIRE(report.has_value());
    CHECK_EQ(report.value().warnings.size(), std::size_t{2});  // the cycle-closing link and the missing parent
    CHECK(h::validate(w).empty());
}

AETHER_TEST(level_migrations_run_in_order) {
    // Two hypothetical future steps: 1 -> 2 renames "entities" to "nodes",
    // 2 -> 3 adds a field. The upgrade must apply both, in order.
    std::vector<int> ran;
    const LevelMigration steps[] = {
        {2, [&](Json& d) -> Result<void> {
             ran.push_back(2);
             if (!d.find("nodes")) return Error{ErrorCode::InvalidArgument, "step 1 did not run first"};
             d.set("units", "meters");
             return ok();
         }},
        {1, [&](Json& d) -> Result<void> {
             ran.push_back(1);
             Json copy = *d.find("entities");
             d.set("nodes", copy);
             return ok();
         }},
    };
    Json doc = parse_json(R"({"format": "aether.level", "version": 1, "entities": []})").value();
    auto from = upgrade_level(doc, steps, 3);
    REQUIRE(from.has_value());
    CHECK_EQ(from.value(), 1);
    CHECK(doc.find("version")->as_number() == 3.0);
    CHECK_EQ(doc.find("units")->as_string(), std::string("meters"));
    CHECK(ran == (std::vector<int>{1, 2}));

    // A gap in the chain is an error.
    Json gap = parse_json(R"({"format": "aether.level", "version": 1, "entities": []})").value();
    const LevelMigration only_second[] = {steps[0]};
    CHECK(!upgrade_level(gap, only_second, 3).has_value());
    // Newer than supported is refused.
    Json newer = parse_json(R"({"version": 5})").value();
    auto r = upgrade_level(newer, {}, 3);
    REQUIRE(!r);
    CHECK(r.error().code == ErrorCode::Unsupported);
    // At the current version nothing runs.
    Json current = parse_json(R"({"version": 1})").value();
    auto same = upgrade_level(current, {});
    REQUIRE(same.has_value());
    CHECK_EQ(same.value(), kLevelFormatVersion);
}

AETHER_TEST(level_files_save_atomically_and_load) {
    const auto reg = registry_with_health();
    World a({.seed = 5});
    make(a, "file entity", {4, 5, 6});
    const auto path = std::filesystem::temp_directory_path() / "aether_test_level.alevel";
    REQUIRE(save_level_file(path, a, reg, "test").has_value());
    CHECK(!std::filesystem::exists(path.string() + ".tmp"));
    World b({.seed = 6});
    auto report = load_level_file(path, b, reg);
    REQUIRE(report.has_value());
    CHECK_EQ(b.entity_count(), std::size_t{1});
    std::filesystem::remove(path);
    World c({.seed = 7});
    auto missing = load_level_file(path, c, reg);
    REQUIRE(!missing);
    CHECK(missing.error().code == ErrorCode::NotFound);
}

AETHER_TEST(level_load_fuzz_never_crashes_or_corrupts) {
    const auto reg = registry_with_health();
    World src({.seed = 8});
    const Entity root = make(src, "root", {1, 1, 1});
    for (int i = 0; i < 6; ++i) {
        const Entity e = make(src, "child", {static_cast<float>(i), 0, 0});
        src.add<Health>(e, Health{i});
        (void)h::set_parent(src, e, root);
    }
    const std::string seed = to_json_string(save_level(src, reg));
    std::mt19937 rng(77);
    const std::string alphabet = "{}[]\":,0123456789-.e abcdef";
    int loaded = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string s = seed;
        for (int k = 0; k < 1 + static_cast<int>(rng() % 3); ++k) {
            const std::size_t pos = rng() % s.size();
            if (rng() % 2) s[pos] = alphabet[rng() % alphabet.size()];
            else s.erase(pos, 1);
        }
        auto doc = parse_json(s);
        if (!doc) continue;
        World w({.seed = 9});
        auto r = load_level(doc.value(), w, reg);
        if (r) {
            ++loaded;
            CHECK(h::validate(w).empty());
        } else {
            CHECK_EQ(w.entity_count(), std::size_t{0});  // rolled back
        }
    }
    std::printf("    %d mutated levels still loaded\n", loaded);
}
