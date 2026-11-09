// Stable names for GPU meshes.
//
// MeshIds are runtime handles from Renderer::upload_mesh; level files refer
// to meshes by name ("builtin:cube", later asset paths). Applications
// register the meshes they upload here, and the MeshInstance serializer
// translates between the two.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "aether/render/mesh.hpp"
#include "aether/world/serialization.hpp"

namespace aether::render {

inline constexpr std::string_view kBuiltinCube = "builtin:cube";

class MeshRegistry {
public:
    void add(std::string name, MeshId id);
    [[nodiscard]] std::optional<MeshId> find(std::string_view name) const;
    [[nodiscard]] std::optional<std::string> name_of(MeshId id) const;

private:
    std::unordered_map<std::string, MeshId> by_name_;
    std::unordered_map<MeshId, std::string> by_id_;
};

// MeshInstance in level files: {"mesh": "<name>", "color": [r, g, b]}.
// `meshes` must outlive the registry. An unknown mesh name fails that
// component's load (the loader keeps the data and warns).
void register_components(world::SerializerRegistry& registry, const MeshRegistry& meshes);

}  // namespace aether::render
