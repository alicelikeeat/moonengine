// Imported 3D models (glTF 2.0).
//
// load_gltf_file() reads .gltf (JSON + external or embedded buffers) and
// .glb (binary container) files into a renderer-independent Model. Files
// are untrusted input: every buffer view and accessor is bounds-checked,
// index values are validated, node graphs must be acyclic, external URIs
// may not leave the model's folder, and sizes are limited.
//
// Supported: triangle meshes with POSITION, NORMAL, TANGENT, TEXCOORD_0 and
// indices; metallic-roughness materials (base colour, metallic, roughness,
// emissive factors; base colour, metallic-roughness and normal textures in
// PNG/JPEG); node hierarchy with TRS or matrix transforms; the default
// scene. Not yet: skinning, morph targets, animations, sparse accessors,
// occlusion and emissive textures (warned, ignored).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aether/foundation/math.hpp"
#include "aether/foundation/result.hpp"

namespace aether::assets {

struct TextureImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // RGBA8, top row first
};

// glTF metallic-roughness material. Texture fields index Model::textures
// (-1: none). Defaults follow the glTF specification.
struct Material {
    std::string name;
    std::array<float, 4> base_color{1, 1, 1, 1};  // linear RGBA
    int base_color_texture = -1;                   // sRGB colour
    float metallic = 1.0f;
    float roughness = 1.0f;
    int metallic_roughness_texture = -1;           // linear; G = roughness, B = metallic
    int normal_texture = -1;                       // linear tangent-space normal map
    float normal_scale = 1.0f;
    math::Vec3 emissive{0, 0, 0};                  // linear RGB
};

struct Primitive {
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;               // same count as positions
    std::vector<std::array<float, 2>> uvs;         // same count as positions
    // Same count as positions: xyz tangent, w = handedness, so that the
    // bitangent is cross(normal, tangent) * w and points toward decreasing
    // v (up in the image), as glTF normal maps expect. Read from TANGENT or
    // generated from the UVs.
    std::vector<std::array<float, 4>> tangents;
    std::vector<std::uint32_t> indices;            // triangle list
    int material = -1;                             // index into Model::materials
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
};

struct Node {
    std::string name;
    math::Vec3 translation{0, 0, 0};
    math::Quat rotation{};
    math::Vec3 scale{1, 1, 1};
    int mesh = -1;
    std::vector<int> children;
};

struct Model {
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<TextureImage> textures;  // one per glTF texture
    std::vector<Node> nodes;
    std::vector<int> roots;              // nodes of the default scene
    std::vector<std::string> warnings;   // unsupported features that were skipped
};

struct ImportOptions {
    std::size_t max_file_bytes = 512u * 1024u * 1024u;
    std::size_t max_vertices = 20'000'000;    // total over all primitives
    std::uint32_t max_texture_size = 16384;   // width or height
};

[[nodiscard]] Result<Model> load_gltf_file(const std::filesystem::path& path, const ImportOptions& options = {});

// In-memory variants. `base_dir` resolves relative buffer/image URIs; pass
// an empty path to forbid external files.
[[nodiscard]] Result<Model> parse_gltf(std::string_view json, const std::filesystem::path& base_dir,
                                       const ImportOptions& options = {});
[[nodiscard]] Result<Model> parse_glb(std::span<const std::uint8_t> bytes, const std::filesystem::path& base_dir,
                                      const ImportOptions& options = {});

// Helpers, exposed for tests and other importers.
// Fills prim.tangents from positions, normals, UVs and indices (per-triangle
// UV gradients, accumulated and orthogonalised per vertex). Vertices with
// degenerate UVs get any tangent perpendicular to the normal.
void generate_tangents(Primitive& prim);

[[nodiscard]] Result<std::vector<std::uint8_t>> decode_base64(std::string_view text);
[[nodiscard]] Result<TextureImage> decode_image(std::span<const std::uint8_t> bytes, std::uint32_t max_size = 16384);

}  // namespace aether::assets
