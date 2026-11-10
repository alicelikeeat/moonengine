// Imported models on the GPU, by content path.
//
// A ModelLibrary owns a content folder. load("models/crate.glb") imports the
// file once (assets::load_gltf_file), uploads its primitives and textures and
// registers stable names for them in names():
//
//   asset:models/crate.glb#mesh<m>.<p>    primitive p of glTF mesh m
//   asset:models/crate.glb#texture<t>     glTF texture t as colour (sRGB)
//   asset:models/crate.glb#texture<t>.linear   the same texture as data
//                                         (normal and metallic-roughness maps)
//
// Level files store those names, and names() resolves them on demand, so a
// level that uses a model loads it when the level is opened. The library
// also registers "builtin:cube" and keeps local bounds per MeshId for
// picking.
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aether/foundation/log.hpp"
#include "aether/foundation/result.hpp"
#include "aether/render/mesh_registry.hpp"
#include "aether/render/renderer.hpp"
#include "aether/world/world.hpp"

namespace aether::render {

// One drawable primitive with its material, ready to become a MeshInstance.
struct ModelPart {
    MeshId mesh = 0;
    MeshInstance material;  // `mesh` is set too
};

struct ModelNode {
    std::string name;
    math::Vec3 translation{};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    std::vector<ModelPart> parts;  // drawable primitives of the node's mesh
    std::vector<int> children;
};

struct LoadedModel {
    std::string path;              // content-relative, '/' separators
    std::vector<ModelNode> nodes;
    std::vector<int> roots;
    Aabb bounds;                   // all parts in model space
    std::size_t triangles = 0;
    std::vector<std::string> warnings;
};

class ModelLibrary {
public:
    // Uploads the builtin cube and sphere; fails only if an upload fails.
    static Result<std::unique_ptr<ModelLibrary>> create(Renderer& renderer, std::filesystem::path content_root,
                                                        Logger* logger = nullptr);
    ~ModelLibrary();
    ModelLibrary(const ModelLibrary&) = delete;
    ModelLibrary& operator=(const ModelLibrary&) = delete;

    [[nodiscard]] const std::filesystem::path& content_root() const noexcept { return root_; }
    [[nodiscard]] MeshId cube() const noexcept { return cube_; }
    [[nodiscard]] MeshId sphere() const noexcept { return sphere_; }  // "builtin:sphere", radius 0.5
    [[nodiscard]] const MeshRegistry& names() const noexcept { return names_; }
    // Local bounds indexed by MeshId (zero box for ids it did not upload).
    [[nodiscard]] const std::vector<Aabb>& bounds() const noexcept { return bounds_; }

    // `asset_path` is relative to the content root and may not leave it.
    // Results are cached, including failures (until refresh()).
    Result<const LoadedModel*> load(std::string_view asset_path);

    // .gltf and .glb files under the content root, sorted, '/' separators.
    [[nodiscard]] std::vector<std::string> list_models() const;
    // Forgets cached failures so fixed files can be retried. Loaded models
    // stay loaded (their GPU data is referenced by entities).
    void refresh();

    // Instantiates a model: a root entity named after the file, one entity
    // per glTF node (Name + Transform, MeshInstance for single-primitive
    // nodes, a child per primitive otherwise). The model's lowest point
    // rests at `position`.y. Run propagate_transforms() afterwards.
    Result<world::Entity> spawn(world::World& world, std::string_view asset_path, math::Vec3 position);

private:
    ModelLibrary(Renderer& renderer, std::filesystem::path root, Logger* logger);
    Result<LoadedModel> import(const std::string& path);
    void resolve(std::string_view name);
    void track_bounds(MeshId id, const Aabb& box);

    Renderer& renderer_;
    std::filesystem::path root_;
    Logger* logger_;
    MeshRegistry names_;
    MeshId cube_ = 0;
    MeshId sphere_ = 0;
    std::vector<Aabb> bounds_;
    std::unordered_map<std::string, std::unique_ptr<LoadedModel>> loaded_;
    std::unordered_map<std::string, Error> failed_;
};

// The content folder for a program: `explicit_root` if given, else
// "Content" in the working directory, else "Content" next to the
// executable (`argv0`), else the working directory's "Content" (missing).
[[nodiscard]] std::filesystem::path find_content_root(const std::filesystem::path& explicit_root, const char* argv0);

// Normalises a content-relative path ("a\\b/../c.glb" -> "a/c.glb"). Fails
// for absolute paths and paths that leave the root.
[[nodiscard]] Result<std::string> normalize_asset_path(std::string_view path);

}  // namespace aether::render
