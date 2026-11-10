#include "aether/render/mesh_registry.hpp"

#include "aether/render/light.hpp"

namespace aether::render {

namespace {

template <typename Id>
std::optional<Id> lookup(const std::unordered_map<std::string, Id>& map, std::string_view name) {
    const auto it = map.find(std::string(name));
    if (it == map.end()) return std::nullopt;
    return it->second;
}

}  // namespace

void MeshRegistry::add(std::string name, MeshId id) {
    by_id_[id] = name;
    by_name_[std::move(name)] = id;
}

std::optional<MeshId> MeshRegistry::find(std::string_view name) const {
    if (auto id = lookup(by_name_, name)) return id;
    if (!resolver_) return std::nullopt;
    resolver_(name);
    return lookup(by_name_, name);
}

std::optional<std::string> MeshRegistry::name_of(MeshId id) const {
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second;
}

void MeshRegistry::add_texture(std::string name, TextureId id) {
    texture_by_id_[id] = name;
    texture_by_name_[std::move(name)] = id;
}

std::optional<TextureId> MeshRegistry::find_texture(std::string_view name) const {
    if (name == kBuiltinWhite) return kWhiteTexture;
    if (auto id = lookup(texture_by_name_, name)) return id;
    if (!resolver_) return std::nullopt;
    resolver_(name);
    return lookup(texture_by_name_, name);
}

std::optional<std::string> MeshRegistry::texture_name_of(TextureId id) const {
    if (id == kWhiteTexture) return std::string(kBuiltinWhite);
    const auto it = texture_by_id_.find(id);
    if (it == texture_by_id_.end()) return std::nullopt;
    return it->second;
}

namespace {

Result<float> read_unit(const Json& j, std::string_view key, float fallback) {
    auto v = world::json_io::read_number(j, key, fallback);
    if (!v) return v.error();
    if (!(v.value() >= 0.0 && v.value() <= 1.0))
        return Error{ErrorCode::InvalidArgument, "\"" + std::string(key) + "\" must be between 0 and 1"};
    return static_cast<float>(v.value());
}

Result<TextureId> read_texture(const MeshRegistry& m, const Json& j, std::string_view key) {
    auto name = world::json_io::read_string(j, key, std::string(kBuiltinWhite));
    if (!name) return name.error();
    const auto id = m.find_texture(name.value());
    if (!id) return Error{ErrorCode::NotFound, "unknown texture \"" + name.value() + "\""};
    return *id;
}

}  // namespace

void register_components(world::SerializerRegistry& registry, const MeshRegistry& meshes) {
    const MeshRegistry* m = &meshes;
    const MeshInstance defaults;
    registry.add<MeshInstance>(
        [m, defaults](const MeshInstance& inst) {
            const auto texture_name = [m](TextureId t) {
                return m->texture_name_of(t).value_or("texture:" + std::to_string(t));
            };
            Json j = Json::object();
            j.set("mesh", m->name_of(inst.mesh).value_or("mesh:" + std::to_string(inst.mesh)));
            j.set("color", world::json_io::from_vec3(inst.color));
            // Material fields are written only when they differ from the
            // defaults, so simple levels stay short.
            if (inst.texture != kWhiteTexture) j.set("texture", texture_name(inst.texture));
            if (inst.metallic != defaults.metallic) j.set("metallic", world::json_io::from_float(inst.metallic));
            if (inst.roughness != defaults.roughness) j.set("roughness", world::json_io::from_float(inst.roughness));
            if (!(inst.emissive == defaults.emissive)) j.set("emissive", world::json_io::from_vec3(inst.emissive));
            if (inst.normal_texture != kWhiteTexture) j.set("normal_texture", texture_name(inst.normal_texture));
            if (inst.metallic_roughness_texture != kWhiteTexture)
                j.set("metallic_roughness_texture", texture_name(inst.metallic_roughness_texture));
            return j;
        },
        [m, defaults](const Json& j) -> Result<MeshInstance> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "MeshInstance must be an object"};
            MeshInstance inst;
            auto name = world::json_io::read_string(j, "mesh", std::string(kBuiltinCube));
            if (!name) return name.error();
            const auto id = m->find(name.value());
            if (!id) return Error{ErrorCode::NotFound, "unknown mesh \"" + name.value() + "\""};
            inst.mesh = *id;
            auto color = world::json_io::read_vec3(j, "color", defaults.color);
            if (!color) return color.error();
            inst.color = color.value();
            auto metallic = read_unit(j, "metallic", defaults.metallic);
            if (!metallic) return metallic.error();
            inst.metallic = metallic.value();
            auto roughness = read_unit(j, "roughness", defaults.roughness);
            if (!roughness) return roughness.error();
            inst.roughness = roughness.value();
            auto emissive = world::json_io::read_vec3(j, "emissive", defaults.emissive);
            if (!emissive) return emissive.error();
            if (emissive.value().x < 0 || emissive.value().y < 0 || emissive.value().z < 0)
                return Error{ErrorCode::InvalidArgument, "\"emissive\" must not be negative"};
            inst.emissive = emissive.value();
            auto texture = read_texture(*m, j, "texture");
            if (!texture) return texture.error();
            inst.texture = texture.value();
            auto normal = read_texture(*m, j, "normal_texture");
            if (!normal) return normal.error();
            inst.normal_texture = normal.value();
            auto mr = read_texture(*m, j, "metallic_roughness_texture");
            if (!mr) return mr.error();
            inst.metallic_roughness_texture = mr.value();
            return inst;
        });

    registry.add<Light>(
        [](const Light& light) {
            Json j = Json::object();
            j.set("type", light.type == LightType::Directional ? "directional" : "point");
            j.set("color", world::json_io::from_vec3(light.color));
            j.set("intensity", world::json_io::from_float(light.intensity));
            if (light.type == LightType::Point) j.set("range", world::json_io::from_float(light.range));
            if (light.type == LightType::Directional) j.set("shadows", light.cast_shadows);
            return j;
        },
        [](const Json& j) -> Result<Light> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "Light must be an object"};
            Light light;
            auto type = world::json_io::read_string(j, "type", "point");
            if (!type) return type.error();
            if (type.value() == "directional") light.type = LightType::Directional;
            else if (type.value() == "point") light.type = LightType::Point;
            else return Error{ErrorCode::InvalidArgument, "unknown light type \"" + type.value() + "\""};
            auto color = world::json_io::read_vec3(j, "color", light.color);
            if (!color) return color.error();
            light.color = color.value();
            if (light.type == LightType::Directional) light.intensity = 3.0f;
            auto intensity = world::json_io::read_number(j, "intensity", light.intensity);
            if (!intensity) return intensity.error();
            auto range = world::json_io::read_number(j, "range", light.range);
            if (!range) return range.error();
            if (intensity.value() < 0 || range.value() <= 0)
                return Error{ErrorCode::InvalidArgument, "Light intensity must be >= 0 and range > 0"};
            light.intensity = static_cast<float>(intensity.value());
            light.range = static_cast<float>(range.value());
            if (const Json* shadows = j.find("shadows")) {
                if (!shadows->is_bool()) return Error{ErrorCode::InvalidArgument, "\"shadows\" must be true or false"};
                light.cast_shadows = shadows->as_bool();
            }
            return light;
        });
}

}  // namespace aether::render
