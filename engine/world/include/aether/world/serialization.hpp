// Level serialization (format "aether.level", see docs/formats/level-v1.md).
//
// Each module registers how its components are written to and read from
// JSON in a SerializerRegistry; World itself knows nothing about specific
// components. Levels store entities by Uuid with an optional parent Uuid,
// parents before children, so hierarchy and child order round-trip.
//
// Loading treats the file as untrusted: every field is type-checked, a
// fatal error rolls back all entities created by the load, and component
// data that cannot be read (unknown type, or invalid for its type) is kept
// verbatim in UnknownComponents so saving again does not lose it.
#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aether/foundation/json.hpp"
#include "aether/foundation/math.hpp"
#include "aether/foundation/result.hpp"
#include "aether/world/world.hpp"

namespace aether::world {

inline constexpr std::string_view kLevelFormat = "aether.level";
inline constexpr int kLevelFormatVersion = 1;
inline constexpr std::string_view kLevelFileExtension = ".alevel";

struct ComponentSerializer {
    std::string name;  // the component's kComponentName
    std::function<bool(const World&, Entity)> has;
    std::function<Json(const World&, Entity)> save;
    std::function<Result<void>(World&, Entity, const Json&)> load;
};

class SerializerRegistry {
public:
    // Adds or replaces the serializer for `serializer.name`.
    void add(ComponentSerializer serializer);

    // Convenience for a component type T with kComponentName.
    template <Component T>
    void add(std::function<Json(const T&)> save, std::function<Result<T>(const Json&)> load) {
        ComponentSerializer s;
        s.name = std::string(T::kComponentName);
        s.has = [](const World& w, Entity e) { return w.has<T>(e); };
        s.save = [save](const World& w, Entity e) { return save(*w.get<T>(e)); };
        s.load = [load](World& w, Entity e, const Json& j) -> Result<void> {
            auto value = load(j);
            if (!value) return value.error();
            w.add<T>(e, std::move(value).value());
            return ok();
        };
        add(std::move(s));
    }

    [[nodiscard]] const ComponentSerializer* find(std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<ComponentSerializer>& all() const noexcept { return serializers_; }

private:
    std::vector<ComponentSerializer> serializers_;
};

// Component data this build could not interpret, kept for re-saving.
struct UnknownComponents {
    static constexpr std::string_view kComponentName = "UnknownComponents";
    Json::Object components;
};

// Registers Name and Transform.
void register_core_components(SerializerRegistry& registry);

// Upgrades a level document from `from_version` to `from_version + 1`.
struct LevelMigration {
    int from_version = 0;
    std::function<Result<void>(Json& document)> migrate;
};

// Applies migrations to `document` until it reaches `target_version`
// (normally kLevelFormatVersion), one version at a time, updating
// "version". Returns the version the document started at.
[[nodiscard]] Result<int> upgrade_level(Json& document, std::span<const LevelMigration> migrations,
                                        int target_version = kLevelFormatVersion);

struct LoadReport {
    std::size_t entities = 0;
    int file_version = 0;
    std::vector<std::string> warnings;
};

// Serialises every live entity. `generator` is recorded for diagnostics.
[[nodiscard]] Json save_level(World& world, const SerializerRegistry& registry, std::string_view generator = {});

// Adds the level's entities to `world` (normally an empty world). Entity
// UUIDs must not already exist in `world`.
[[nodiscard]] Result<LoadReport> load_level(const Json& document, World& world, const SerializerRegistry& registry,
                                            std::span<const LevelMigration> migrations = {});

// File helpers: UTF-8 JSON, written atomically (temporary file + rename).
[[nodiscard]] Result<void> save_level_file(const std::filesystem::path& path, World& world,
                                           const SerializerRegistry& registry, std::string_view generator = {});
[[nodiscard]] Result<LoadReport> load_level_file(const std::filesystem::path& path, World& world,
                                                 const SerializerRegistry& registry);

// JSON helpers for component serializers.
namespace json_io {
// A float written with the fewest digits that read back as the same float
// ("2.2", not "2.200000047683716"), keeping level files readable.
[[nodiscard]] Json from_float(float f);
[[nodiscard]] Json from_vec3(const math::Vec3& v);
[[nodiscard]] Json from_quat(const math::Quat& q);
// Reads member `key` of `object`; returns `fallback` when absent.
[[nodiscard]] Result<math::Vec3> read_vec3(const Json& object, std::string_view key, math::Vec3 fallback);
[[nodiscard]] Result<math::Quat> read_quat(const Json& object, std::string_view key, math::Quat fallback);
[[nodiscard]] Result<double> read_number(const Json& object, std::string_view key, double fallback);
[[nodiscard]] Result<std::string> read_string(const Json& object, std::string_view key, std::string fallback);
}  // namespace json_io

}  // namespace aether::world
