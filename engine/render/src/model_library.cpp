#include "aether/render/model_library.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <system_error>

#include "aether/assets/model.hpp"
#include "aether/world/hierarchy.hpp"
#include "aether/world/name.hpp"

namespace aether::render {

namespace {

constexpr std::string_view kAssetPrefix = "asset:";
constexpr std::size_t kMaxListedModels = 10000;

void grow(Aabb& box, bool& empty, math::Vec3 p) {
    if (empty) {
        box = {p, p};
        empty = false;
        return;
    }
    box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
    box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

MeshData to_mesh_data(const assets::Primitive& prim) {
    MeshData out;
    out.vertices.resize(prim.positions.size());
    for (std::size_t i = 0; i < prim.positions.size(); ++i) {
        Vertex& v = out.vertices[i];
        v.position = prim.positions[i];
        v.normal = i < prim.normals.size() ? prim.normals[i] : math::Vec3{0, 1, 0};
        if (i < prim.uvs.size()) {
            v.uv[0] = prim.uvs[i][0];
            v.uv[1] = prim.uvs[i][1];
        }
        if (i < prim.tangents.size())
            for (int k = 0; k < 4; ++k) v.tangent[k] = prim.tangents[i][static_cast<std::size_t>(k)];
    }
    out.indices = prim.indices;
    return out;
}

}  // namespace

Result<std::string> normalize_asset_path(std::string_view path) {
    std::string p(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.empty()) return Error{ErrorCode::InvalidArgument, "empty asset path"};
    if (p.front() == '/' || (p.size() >= 2 && p[1] == ':'))
        return Error{ErrorCode::InvalidArgument, "asset path must be relative: " + p};
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= p.size()) {
        const std::size_t end = std::min(p.find('/', start), p.size());
        std::string part = p.substr(start, end - start);
        if (part == "..") {
            if (parts.empty()) return Error{ErrorCode::PermissionDenied, "asset path leaves the content folder: " + p};
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(std::move(part));
        }
        start = end + 1;
    }
    if (parts.empty()) return Error{ErrorCode::InvalidArgument, "empty asset path"};
    std::string out;
    for (const auto& part : parts) out += (out.empty() ? "" : "/") + part;
    return out;
}

std::filesystem::path find_content_root(const std::filesystem::path& explicit_root, const char* argv0) {
    if (!explicit_root.empty()) return explicit_root;
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec) / "Content";
    if (std::filesystem::is_directory(cwd, ec)) return cwd;
    if (argv0 && *argv0) {
        const std::filesystem::path beside = std::filesystem::absolute(argv0, ec).parent_path() / "Content";
        if (!ec && std::filesystem::is_directory(beside, ec)) return beside;
    }
    return cwd;
}

ModelLibrary::ModelLibrary(Renderer& renderer, std::filesystem::path root, Logger* logger)
    : renderer_(renderer), root_(std::move(root)), logger_(logger) {}

ModelLibrary::~ModelLibrary() = default;

Result<std::unique_ptr<ModelLibrary>> ModelLibrary::create(Renderer& renderer, std::filesystem::path content_root,
                                                          Logger* logger) {
    std::unique_ptr<ModelLibrary> lib(new ModelLibrary(renderer, std::move(content_root), logger));
    const MeshData cube = make_cube(1.0f);
    auto id = renderer.upload_mesh(cube);
    if (!id) return id.error();
    lib->cube_ = id.value();
    lib->track_bounds(lib->cube_, compute_bounds(cube));
    lib->names_.add(std::string(kBuiltinCube), lib->cube_);
    ModelLibrary* self = lib.get();
    lib->names_.set_resolver([self](std::string_view name) { self->resolve(name); });
    return lib;
}

void ModelLibrary::track_bounds(MeshId id, const Aabb& box) {
    if (id >= bounds_.size()) bounds_.resize(std::size_t{id} + 1);
    bounds_[id] = box;
}

void ModelLibrary::resolve(std::string_view name) {
    if (name.substr(0, kAssetPrefix.size()) != kAssetPrefix) return;
    name.remove_prefix(kAssetPrefix.size());
    const std::size_t hash = name.find('#');
    if (hash == std::string_view::npos) return;
    auto r = load(name.substr(0, hash));
    if (!r && logger_)
        AETHER_LOG(*logger_, LogLevel::Warn, "assets", "cannot load {}: {}", name.substr(0, hash), r.error().message);
}

Result<const LoadedModel*> ModelLibrary::load(std::string_view asset_path) {
    auto path = normalize_asset_path(asset_path);
    if (!path) return path.error();
    if (auto it = loaded_.find(path.value()); it != loaded_.end()) return it->second.get();
    if (auto it = failed_.find(path.value()); it != failed_.end()) return it->second;
    auto model = import(path.value());
    if (!model) {
        failed_.emplace(path.value(), model.error());
        return model.error();
    }
    auto& slot = loaded_[path.value()];
    slot = std::make_unique<LoadedModel>(std::move(model).value());
    if (logger_) {
        AETHER_LOG(*logger_, LogLevel::Info, "assets", "loaded {} ({} nodes, {} triangles)", slot->path,
                   slot->nodes.size(), slot->triangles);
        for (const auto& w : slot->warnings) AETHER_LOG(*logger_, LogLevel::Warn, "assets", "{}: {}", slot->path, w);
    }
    return slot.get();
}

Result<LoadedModel> ModelLibrary::import(const std::string& path) {
    auto model = assets::load_gltf_file(root_ / std::filesystem::path(path));
    if (!model) return model.error();
    const assets::Model& src = model.value();

    LoadedModel out;
    out.path = path;
    out.warnings = src.warnings;
    const std::string prefix = std::string(kAssetPrefix) + path + "#";

    // Each texture is uploaded in the colour space its materials need:
    // sRGB for base colour, linear for normal and metallic-roughness data.
    std::vector<bool> as_color(src.textures.size(), false), as_data(src.textures.size(), false);
    const auto mark = [&](std::vector<bool>& v, int t) {
        if (t >= 0 && static_cast<std::size_t>(t) < v.size()) v[static_cast<std::size_t>(t)] = true;
    };
    for (const auto& mat : src.materials) {
        mark(as_color, mat.base_color_texture);
        mark(as_data, mat.normal_texture);
        mark(as_data, mat.metallic_roughness_texture);
    }
    std::vector<TextureId> color_textures(src.textures.size(), kWhiteTexture), data_textures = color_textures;
    for (std::size_t t = 0; t < src.textures.size(); ++t) {
        const auto& img = src.textures[t];
        for (const bool srgb : {true, false}) {
            if (srgb ? !as_color[t] : !as_data[t]) continue;
            auto id = renderer_.upload_texture(TextureData{img.width, img.height, img.rgba, srgb});
            if (!id) {
                out.warnings.push_back(std::format("texture {} not uploaded: {}", t, id.error().message));
                continue;
            }
            (srgb ? color_textures : data_textures)[t] = id.value();
            names_.add_texture(prefix + "texture" + std::to_string(t) + (srgb ? "" : ".linear"), id.value());
        }
    }
    const auto texture = [](const std::vector<TextureId>& ids, int t) {
        return t >= 0 && static_cast<std::size_t>(t) < ids.size() ? ids[static_cast<std::size_t>(t)] : kWhiteTexture;
    };

    // Upload every primitive once; nodes share meshes.
    std::vector<std::vector<ModelPart>> mesh_parts(src.meshes.size());
    std::vector<std::vector<Aabb>> part_bounds(src.meshes.size());
    for (std::size_t m = 0; m < src.meshes.size(); ++m) {
        for (std::size_t p = 0; p < src.meshes[m].primitives.size(); ++p) {
            const auto& prim = src.meshes[m].primitives[p];
            if (prim.indices.empty() || prim.positions.empty()) continue;
            const MeshData data = to_mesh_data(prim);
            auto id = renderer_.upload_mesh(data);
            if (!id) return Error{id.error().code, path + ": " + id.error().message};
            const Aabb box = compute_bounds(data);
            track_bounds(id.value(), box);
            names_.add(prefix + "mesh" + std::to_string(m) + "." + std::to_string(p), id.value());
            ModelPart part;
            part.mesh = id.value();
            part.material.mesh = id.value();
            // glTF's default material: white, fully rough, non-metallic.
            part.material.color = {1, 1, 1};
            part.material.metallic = 0.0f;
            part.material.roughness = 1.0f;
            if (prim.material >= 0 && static_cast<std::size_t>(prim.material) < src.materials.size()) {
                const auto& mat = src.materials[static_cast<std::size_t>(prim.material)];
                MeshInstance& out_mat = part.material;
                out_mat.color = {mat.base_color[0], mat.base_color[1], mat.base_color[2]};
                out_mat.metallic = mat.metallic;
                out_mat.roughness = mat.roughness;
                out_mat.emissive = mat.emissive;
                out_mat.texture = texture(color_textures, mat.base_color_texture);
                out_mat.normal_texture = texture(data_textures, mat.normal_texture);
                out_mat.metallic_roughness_texture = texture(data_textures, mat.metallic_roughness_texture);
            }
            mesh_parts[m].push_back(part);
            part_bounds[m].push_back(box);
            out.triangles += prim.indices.size() / 3;
        }
    }

    out.nodes.resize(src.nodes.size());
    for (std::size_t n = 0; n < src.nodes.size(); ++n) {
        const auto& sn = src.nodes[n];
        ModelNode& node = out.nodes[n];
        node.name = sn.name.empty() ? "Node " + std::to_string(n) : sn.name;
        node.translation = sn.translation;
        node.rotation = sn.rotation;
        node.scale = sn.scale;
        node.children = sn.children;
        if (sn.mesh >= 0 && static_cast<std::size_t>(sn.mesh) < mesh_parts.size())
            node.parts = mesh_parts[static_cast<std::size_t>(sn.mesh)];
    }
    out.roots = src.roots;

    // Model-space bounds: walk the scene with accumulated transforms. The
    // importer guarantees an acyclic graph.
    bool empty = true;
    struct Item {
        int node;
        math::Mat4 parent;
    };
    std::vector<Item> stack;
    for (int r : out.roots) stack.push_back({r, math::Mat4{}});
    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        const auto& sn = src.nodes[static_cast<std::size_t>(item.node)];
        const math::Mat4 world = item.parent * math::Mat4::from_trs(sn.translation, sn.rotation, sn.scale);
        if (sn.mesh >= 0 && static_cast<std::size_t>(sn.mesh) < part_bounds.size()) {
            for (const Aabb& b : part_bounds[static_cast<std::size_t>(sn.mesh)]) {
                for (int c = 0; c < 8; ++c) {
                    const math::Vec3 corner{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y,
                                            (c & 4) ? b.max.z : b.min.z};
                    grow(out.bounds, empty, world.transform_point(corner));
                }
            }
        }
        for (int child : sn.children) stack.push_back({child, world});
    }
    return out;
}

std::vector<std::string> ModelLibrary::list_models() const {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root_, ec)) return out;
    std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied,
                                                     ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext != ".gltf" && ext != ".glb") continue;
        out.push_back(std::filesystem::relative(it->path(), root_, ec).generic_string());
        if (out.size() >= kMaxListedModels) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

void ModelLibrary::refresh() { failed_.clear(); }

Result<world::Entity> ModelLibrary::spawn(world::World& w, std::string_view asset_path, math::Vec3 position) {
    auto loaded = load(asset_path);
    if (!loaded) return loaded.error();
    const LoadedModel& model = *loaded.value();

    const world::Entity root = w.create();
    w.add<world::Name>(root, world::Name{std::filesystem::path(model.path).stem().string()});
    w.add<world::Transform>(root, world::Transform{{position.x, position.y - model.bounds.min.y, position.z}});

    auto add_child = [&](world::Entity parent, std::string name, world::Transform t) -> Result<world::Entity> {
        const world::Entity e = w.create();
        w.add<world::Name>(e, world::Name{std::move(name)});
        w.add<world::Transform>(e, t);
        auto linked = world::hierarchy::set_parent(w, e, parent);
        if (!linked) return linked.error();
        return e;
    };

    struct Item {
        int node;
        world::Entity parent;
    };
    std::vector<Item> stack;
    for (auto r = model.roots.rbegin(); r != model.roots.rend(); ++r) stack.push_back({*r, root});
    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        const ModelNode& node = model.nodes[static_cast<std::size_t>(item.node)];
        auto e = add_child(item.parent, node.name, world::Transform{node.translation, node.rotation, node.scale});
        if (!e) return e.error();
        if (node.parts.size() == 1) {
            w.add<MeshInstance>(e.value(), node.parts.front().material);
        } else {
            for (std::size_t p = 0; p < node.parts.size(); ++p) {
                auto child = add_child(e.value(), node.name + " [" + std::to_string(p) + "]", world::Transform{});
                if (!child) return child.error();
                w.add<MeshInstance>(child.value(), node.parts[p].material);
            }
        }
        for (auto c = node.children.rbegin(); c != node.children.rend(); ++c) stack.push_back({*c, e.value()});
    }
    return root;
}

}  // namespace aether::render
