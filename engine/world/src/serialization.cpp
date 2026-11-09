#include "aether/world/serialization.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>

#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"

namespace aether::world {

namespace h = hierarchy;

// ---- Registry ---------------------------------------------------------------

void SerializerRegistry::add(ComponentSerializer serializer) {
    for (auto& s : serializers_)
        if (s.name == serializer.name) {
            s = std::move(serializer);
            return;
        }
    serializers_.push_back(std::move(serializer));
}

const ComponentSerializer* SerializerRegistry::find(std::string_view name) const noexcept {
    for (const auto& s : serializers_)
        if (s.name == name) return &s;
    return nullptr;
}

// ---- JSON helpers -------------------------------------------------------------

namespace json_io {

Json from_float(float f) {
    char buf[32];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, f);  // shortest float form
    double d = static_cast<double>(f);
    if (ec == std::errc()) std::from_chars(buf, end, d);
    return Json(d);
}

Json from_vec3(const math::Vec3& v) { return Json(Json::Array{from_float(v.x), from_float(v.y), from_float(v.z)}); }

Json from_quat(const math::Quat& q) {
    return Json(Json::Array{from_float(q.x), from_float(q.y), from_float(q.z), from_float(q.w)});
}

namespace {
Result<std::vector<float>> read_floats(const Json& object, std::string_view key, std::size_t count) {
    const Json* v = object.find(key);
    if (!v || !v->is_array() || v->as_array().size() != count)
        return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must be an array of " +
                                                     std::to_string(count) + " numbers"};
    std::vector<float> out;
    for (const Json& item : v->as_array()) {
        if (!item.is_number()) return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must contain numbers"};
        const double d = item.as_number();
        if (std::fabs(d) > 3.0e38) return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" value out of range"};
        out.push_back(static_cast<float>(d));
    }
    return out;
}
}  // namespace

Result<math::Vec3> read_vec3(const Json& object, std::string_view key, math::Vec3 fallback) {
    if (!object.find(key)) return fallback;
    auto f = read_floats(object, key, 3);
    if (!f) return f.error();
    return math::Vec3{f.value()[0], f.value()[1], f.value()[2]};
}

Result<math::Quat> read_quat(const Json& object, std::string_view key, math::Quat fallback) {
    if (!object.find(key)) return fallback;
    auto f = read_floats(object, key, 4);
    if (!f) return f.error();
    const math::Quat q{f.value()[0], f.value()[1], f.value()[2], f.value()[3]};
    if (q.x == 0 && q.y == 0 && q.z == 0 && q.w == 0)
        return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must not be a zero quaternion"};
    return math::normalize(q);
}

Result<double> read_number(const Json& object, std::string_view key, double fallback) {
    const Json* v = object.find(key);
    if (!v) return fallback;
    if (!v->is_number()) return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must be a number"};
    return v->as_number();
}

Result<std::string> read_string(const Json& object, std::string_view key, std::string fallback) {
    const Json* v = object.find(key);
    if (!v) return fallback;
    if (!v->is_string()) return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must be a string"};
    return v->as_string();
}

}  // namespace json_io

// ---- Core components ------------------------------------------------------------

void register_core_components(SerializerRegistry& registry) {
    registry.add<Name>(
        [](const Name& n) {
            Json j = Json::object();
            j.set("value", n.value);
            return j;
        },
        [](const Json& j) -> Result<Name> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "Name must be an object"};
            auto v = json_io::read_string(j, "value", "");
            if (!v) return v.error();
            return Name{v.value()};
        });
    registry.add<Transform>(
        [](const Transform& t) {
            Json j = Json::object();
            j.set("position", json_io::from_vec3(t.position));
            j.set("rotation", json_io::from_quat(t.rotation));
            j.set("scale", json_io::from_vec3(t.scale));
            return j;
        },
        [](const Json& j) -> Result<Transform> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "Transform must be an object"};
            Transform t;
            auto p = json_io::read_vec3(j, "position", {0, 0, 0});
            if (!p) return p.error();
            auto r = json_io::read_quat(j, "rotation", math::Quat::identity());
            if (!r) return r.error();
            auto s = json_io::read_vec3(j, "scale", {1, 1, 1});
            if (!s) return s.error();
            t.position = p.value();
            t.rotation = r.value();
            t.scale = s.value();
            return t;
        });
}

// ---- Save -----------------------------------------------------------------------

Json save_level(World& world, const SerializerRegistry& registry, std::string_view generator) {
    std::vector<Entity> roots;
    world.for_each_entity([&](Entity e) {
        const Entity p = h::parent_of(world, e);
        if (p.is_null() || !world.alive(p)) roots.push_back(e);
    });
    // Stable order across saves, so level files diff cleanly.
    std::sort(roots.begin(), roots.end(), [&](Entity a, Entity b) { return world.id_of(a) < world.id_of(b); });

    Json entities = Json::array();
    std::vector<Entity> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
        const Entity e = stack.back();
        stack.pop_back();
        Json entry = Json::object();
        entry.set("id", world.id_of(e).to_string());
        const Entity p = h::parent_of(world, e);
        if (!p.is_null() && world.alive(p)) entry.set("parent", world.id_of(p).to_string());
        Json components = Json::object();
        for (const auto& s : registry.all())
            if (s.has(world, e)) components.set(s.name, s.save(world, e));
        if (const auto* unknown = world.get<UnknownComponents>(e))
            for (const auto& m : unknown->components)
                if (!components.find(m.key)) components.set(m.key, m.value);
        entry.set("components", std::move(components));
        entities.push_back(std::move(entry));
        const auto kids = h::children_of(world, e);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            if (world.alive(*it)) stack.push_back(*it);
    }

    Json doc = Json::object();
    doc.set("format", std::string(kLevelFormat));
    doc.set("version", kLevelFormatVersion);
    if (!generator.empty()) doc.set("generator", std::string(generator));
    doc.set("entities", std::move(entities));
    return doc;
}

// ---- Load -----------------------------------------------------------------------

Result<int> upgrade_level(Json& doc, std::span<const LevelMigration> migrations, int target_version) {
    const Json* v = doc.find("version");
    if (!v || !v->is_number() || v->as_number() != std::floor(v->as_number()) || v->as_number() < 0 ||
        v->as_number() > 1'000'000)
        return Error{ErrorCode::InvalidArgument, "\"version\" must be a non-negative integer"};
    const int start = static_cast<int>(v->as_number());
    if (start > target_version)
        return Error{ErrorCode::Unsupported, "level format version " + std::to_string(start) +
                                                 " is newer than this engine supports (" +
                                                 std::to_string(target_version) + "); update Aether"};
    for (int version = start; version < target_version; ++version) {
        const auto it = std::find_if(migrations.begin(), migrations.end(),
                                     [version](const LevelMigration& m) { return m.from_version == version; });
        if (it == migrations.end())
            return Error{ErrorCode::Unsupported, "no migration from level format version " + std::to_string(version)};
        if (auto r = it->migrate(doc); !r) return r.error();
        doc.set("version", version + 1);
    }
    return start;
}

Result<LoadReport> load_level(const Json& input, World& world, const SerializerRegistry& registry,
                              std::span<const LevelMigration> migrations) {
    const auto invalid = [](std::string msg) { return Error{ErrorCode::InvalidArgument, std::move(msg)}; };
    if (!input.is_object()) return invalid("level file must be a JSON object");
    const Json* format = input.find("format");
    if (!format || !format->is_string() || format->as_string() != kLevelFormat)
        return invalid("not an Aether level (expected \"format\": \"aether.level\")");
    const Json* version_json = input.find("version");
    if (!version_json || !version_json->is_number() || version_json->as_number() < 1)
        return invalid("\"version\" must be a positive integer");

    LoadReport report;
    Json doc = input;
    auto upgraded = upgrade_level(doc, migrations);
    if (!upgraded) return upgraded.error();
    report.file_version = upgraded.value();
    if (report.file_version < kLevelFormatVersion)
        report.warnings.push_back("upgraded level from format version " + std::to_string(report.file_version) +
                                  " to " + std::to_string(kLevelFormatVersion));

    const Json* entities = doc.find("entities");
    if (!entities || !entities->is_array()) return invalid("\"entities\" must be an array");

    // Validate identities first so a bad file changes nothing.
    std::vector<Uuid> ids;
    std::unordered_set<Uuid, UuidHash> seen;
    for (std::size_t i = 0; i < entities->as_array().size(); ++i) {
        const Json& e = entities->as_array()[i];
        const std::string where = "entities[" + std::to_string(i) + "]";
        if (!e.is_object()) return invalid(where + " must be an object");
        const Json* id = e.find("id");
        if (!id || !id->is_string()) return invalid(where + ".id must be a UUID string");
        auto uuid = Uuid::parse(id->as_string());
        if (!uuid || uuid.value().is_nil()) return invalid(where + ".id is not a valid UUID");
        if (!seen.insert(uuid.value()).second) return invalid(where + ".id is duplicated: " + id->as_string());
        if (!world.find(uuid.value()).is_null())
            return Error{ErrorCode::AlreadyExists, where + ".id already exists in the world: " + id->as_string()};
        if (const Json* p = e.find("parent"); p && !p->is_string() && !p->is_null())
            return invalid(where + ".parent must be a UUID string");
        if (const Json* c = e.find("components"); c && !c->is_object()) return invalid(where + ".components must be an object");
        ids.push_back(uuid.value());
    }

    std::vector<Entity> created;
    created.reserve(ids.size());
    for (const Uuid& id : ids) {
        auto e = world.create_with_id(id);
        if (!e) {
            for (const Entity c : created) world.destroy(c);
            return e.error();
        }
        created.push_back(e.value());
    }

    std::set<std::string> unknown_names;
    for (std::size_t i = 0; i < created.size(); ++i) {
        const Json* components = entities->as_array()[i].find("components");
        if (!components) continue;
        UnknownComponents unknown;
        for (const auto& m : components->as_object()) {
            const ComponentSerializer* s = registry.find(m.key);
            if (!s) {
                if (unknown_names.insert(m.key).second)
                    report.warnings.push_back("unknown component \"" + m.key + "\" kept as-is");
                unknown.components.push_back(m);
                continue;
            }
            if (auto r = s->load(world, created[i], m.value); !r) {
                report.warnings.push_back("entity " + ids[i].to_string() + ": " + m.key + ": " + r.error().message +
                                          " (data kept as-is)");
                unknown.components.push_back(m);
            }
        }
        if (!unknown.components.empty()) world.add<UnknownComponents>(created[i], std::move(unknown));
    }

    for (std::size_t i = 0; i < created.size(); ++i) {
        const Json* p = entities->as_array()[i].find("parent");
        if (!p || p->is_null()) continue;
        auto parent_id = Uuid::parse(p->as_string());
        const Entity parent = parent_id ? world.find(parent_id.value()) : kNullEntity;
        if (parent.is_null()) {
            report.warnings.push_back("entity " + ids[i].to_string() + ": parent " + p->as_string() +
                                      " not found; loaded as a root");
            continue;
        }
        if (auto r = h::set_parent(world, created[i], parent); !r)
            report.warnings.push_back("entity " + ids[i].to_string() + ": " + r.error().message + "; loaded as a root");
    }
    propagate_transforms(world);
    report.entities = created.size();
    return report;
}

// ---- Files ------------------------------------------------------------------------

Result<void> save_level_file(const std::filesystem::path& path, World& world, const SerializerRegistry& registry,
                             std::string_view generator) {
    const std::string text = to_json_string(save_level(world, registry, generator), 2) + "\n";
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{ErrorCode::IoError, "cannot write " + tmp.string()};
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return Error{ErrorCode::IoError, "write failed: " + tmp.string()};
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return Error{ErrorCode::IoError, "cannot replace " + path.string()};
    }
    return ok();
}

Result<LoadReport> load_level_file(const std::filesystem::path& path, World& world, const SerializerRegistry& registry) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{ErrorCode::NotFound, "cannot open " + path.string()};
    std::ostringstream buf;
    buf << in.rdbuf();
    auto doc = parse_json(buf.str());
    if (!doc) return Error{doc.error().code, path.filename().string() + ": " + doc.error().message};
    return load_level(doc.value(), world, registry);
}

}  // namespace aether::world
