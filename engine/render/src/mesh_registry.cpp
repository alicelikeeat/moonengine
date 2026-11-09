#include "aether/render/mesh_registry.hpp"

namespace aether::render {

void MeshRegistry::add(std::string name, MeshId id) {
    by_id_[id] = name;
    by_name_[std::move(name)] = id;
}

std::optional<MeshId> MeshRegistry::find(std::string_view name) const {
    const auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> MeshRegistry::name_of(MeshId id) const {
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second;
}

void register_components(world::SerializerRegistry& registry, const MeshRegistry& meshes) {
    const MeshRegistry* m = &meshes;
    registry.add<MeshInstance>(
        [m](const MeshInstance& inst) {
            Json j = Json::object();
            j.set("mesh", m->name_of(inst.mesh).value_or("mesh:" + std::to_string(inst.mesh)));
            j.set("color", world::json_io::from_vec3(inst.color));
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
            return MeshInstance{*id, color.value()};
        });
}

}  // namespace aether::render
