// Stable names for GPU meshes and textures.
//
// MeshIds and TextureIds are runtime handles from the Renderer; level files
// refer to them by name ("builtin:cube", "asset:models/crate.glb#mesh0.0").
// Applications register what they upload here, and the MeshInstance
// serializer translates between the two. A resolver can load assets on
// demand when a level names something not yet registered.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "aether/render/mesh.hpp"
#include "aether/world/serialization.hpp"

namespace aether::render {

inline constexpr std::string_view kBuiltinCube = "builtin:cube";
inline constexpr std::string_view kBuiltinWhite = "builtin:white";

class MeshRegistry {
public:
    // Called on a lookup miss with the requested name; it may register the
    // name (for example by loading the asset it refers to). Lookups retry
    // once afterwards.
    using Resolver = std::function<void(std::string_view name)>;

    void add(std::string name, MeshId id);
    [[nodiscard]] std::optional<MeshId> find(std::string_view name) const;
    [[nodiscard]] std::optional<std::string> name_of(MeshId id) const;

    // Texture names. kWhiteTexture is always "builtin:white".
    void add_texture(std::string name, TextureId id);
    [[nodiscard]] std::optional<TextureId> find_texture(std::string_view name) const;
    [[nodiscard]] std::optional<std::string> texture_name_of(TextureId id) const;

    void set_resolver(Resolver resolver) { resolver_ = std::move(resolver); }

private:
    std::unordered_map<std::string, MeshId> by_name_;
    std::unordered_map<MeshId, std::string> by_id_;
    std::unordered_map<std::string, TextureId> texture_by_name_;
    std::unordered_map<TextureId, std::string> texture_by_id_;
    Resolver resolver_;
};

// MeshInstance in level files:
//   {"mesh": "<name>", "color": [r, g, b], "texture": "<name>"}
// "texture" is written only when it is not the white default. `meshes` must
// outlive the registry. An unknown mesh or texture name fails that
// component's load (the loader keeps the data and warns).
void register_components(world::SerializerRegistry& registry, const MeshRegistry& meshes);

}  // namespace aether::render
