#include "aether/render/mesh_registry.hpp"

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

void register_components(world::SerializerRegistry& registry, const MeshRegistry& meshes) {
    const MeshRegistry* m = &meshes;
    registry.add<MeshInstance>(
        [m](const MeshInstance& inst) {
            Json j = Json::object();
            j.set("mesh", m->name_of(inst.mesh).value_or("mesh:" + std::to_string(inst.mesh)));
            j.set("color", world::json_io::from_vec3(inst.color));
            if (inst.texture != kWhiteTexture)
                j.set("texture", m->texture_name_of(inst.texture).value_or("texture:" + std::to_string(inst.texture)));
            return j;
        },
        [m](const Json& j) -> Result<MeshInstance> {
            if (!j.is_object()) return Error{ErrorCode::InvalidArgument, "MeshInstance must be an object"};
            auto name = world::json_io::read_string(j, "mesh", std::string(kBuiltinCube));
            if (!name) return name.error();
            const auto id = m->find(name.value());
            if (!id) return Error{ErrorCode::NotFound, "unknown mesh \"" + name.value() + "\""};
            auto color = world::json_io::read_vec3(j, "color", {0.8f, 0.8f, 0.8f});
            if (!color) return color.error();
            auto texture_name = world::json_io::read_string(j, "texture", std::string(kBuiltinWhite));
            if (!texture_name) return texture_name.error();
            const auto texture = m->find_texture(texture_name.value());
            if (!texture) return Error{ErrorCode::NotFound, "unknown texture \"" + texture_name.value() + "\""};
            return MeshInstance{*id, color.value(), *texture};
        });
}

}  // namespace aether::render
