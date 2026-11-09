// glTF 2.0 importer. See model.hpp for what is supported and the safety rules.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include "aether/assets/model.hpp"
#include "aether/foundation/json.hpp"

namespace aether::assets {

namespace fs = std::filesystem;

namespace {

Error invalid(std::string message) { return Error{ErrorCode::InvalidArgument, "glTF: " + std::move(message)}; }

// ---- JSON field helpers --------------------------------------------------------

const Json::Array* array_field(const Json& obj, std::string_view key) {
    const Json* v = obj.find(key);
    return v && v->is_array() ? &v->as_array() : nullptr;
}

// Non-negative integer field; fallback when absent; nullopt when malformed.
std::optional<std::size_t> index_field(const Json& obj, std::string_view key, std::size_t fallback) {
    const Json* v = obj.find(key);
    if (!v) return fallback;
    if (!v->is_number()) return std::nullopt;
    const double d = v->as_number();
    if (d < 0 || d != std::floor(d) || d > 9.0e15) return std::nullopt;
    return static_cast<std::size_t>(d);
}

std::string string_field(const Json& obj, std::string_view key) {
    const Json* v = obj.find(key);
    return v && v->is_string() ? v->as_string() : std::string();
}

bool read_float_array(const Json& obj, std::string_view key, float* out, std::size_t n) {
    const Json* v = obj.find(key);
    if (!v) return true;  // keep defaults
    if (!v->is_array() || v->as_array().size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Json& e = v->as_array()[i];
        if (!e.is_number() || !std::isfinite(e.as_number())) return false;
        out[i] = static_cast<float>(e.as_number());
    }
    return true;
}

// ---- URIs ----------------------------------------------------------------------

std::optional<std::string> percent_decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size()) return std::nullopt;
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return out;
}

Result<std::vector<std::uint8_t>> read_file(const fs::path& path, std::size_t max_bytes) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) return Error{ErrorCode::NotFound, "cannot open " + path.filename().string()};
    if (size > max_bytes) return Error{ErrorCode::InvalidArgument, path.filename().string() + " exceeds the size limit"};
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{ErrorCode::NotFound, "cannot open " + path.filename().string()};
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!in) return Error{ErrorCode::IoError, "cannot read " + path.filename().string()};
    return data;
}

// Loads a buffer or image URI: data URIs inline, otherwise a file that must
// stay inside base_dir.
Result<std::vector<std::uint8_t>> load_uri(const std::string& uri, const fs::path& base_dir, std::size_t max_bytes) {
    if (uri.rfind("data:", 0) == 0) {
        const auto comma = uri.find(',');
        if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos)
            return invalid("only base64 data URIs are supported");
        if (uri.size() - comma > max_bytes * 4 / 3 + 4) return invalid("data URI exceeds the size limit");
        return decode_base64(std::string_view(uri).substr(comma + 1));
    }
    if (uri.find(':') != std::string::npos) return invalid("URI scheme not allowed: " + uri.substr(0, uri.find(':')));
    if (base_dir.empty()) return invalid("external file references are not allowed here");
    const auto decoded = percent_decode(uri);
    if (!decoded) return invalid("malformed URI " + uri);
    const fs::path rel = fs::path(std::u8string(reinterpret_cast<const char8_t*>(decoded->data()), decoded->size()));
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) return invalid("absolute paths are not allowed");
    for (const auto& part : rel)
        if (part == "..") return invalid("paths outside the model folder are not allowed");
    return read_file(base_dir / rel, max_bytes);
}

// ---- Accessors -----------------------------------------------------------------

struct View {
    const std::uint8_t* data = nullptr;
    std::size_t length = 0;
    std::size_t stride = 0;  // 0 = tightly packed
};

struct Accessor {
    const std::uint8_t* base = nullptr;  // nullptr: all zeros (no bufferView)
    std::size_t count = 0;
    std::size_t components = 0;
    std::size_t component_size = 0;
    std::size_t stride = 0;
    int component_type = 0;
    bool normalized = false;
};

std::size_t component_count(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

std::size_t component_bytes(int t) {
    switch (t) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: return 0;
    }
}

class Importer {
public:
    Importer(const Json& doc, const fs::path& base_dir, const ImportOptions& opt,
             std::span<const std::uint8_t> glb_bin)
        : doc_(doc), base_dir_(base_dir), opt_(opt), glb_bin_(glb_bin) {}

    Result<Model> run() {
        if (!doc_.is_object()) return invalid("document must be an object");
        const Json* asset = doc_.find("asset");
        if (!asset || !asset->is_object() || string_field(*asset, "version").rfind("2.", 0) != 0)
            return invalid("only glTF 2.x is supported");
        if (const auto* req = array_field(doc_, "extensionsRequired"); req && !req->empty())
            return Error{ErrorCode::Unsupported, "glTF: required extension not supported: " +
                                                     (req->front().is_string() ? req->front().as_string() : "?")};
        if (auto r = load_buffers(); !r) return r.error();
        if (auto r = load_views(); !r) return r.error();
        if (auto r = load_textures(); !r) return r.error();
        if (auto r = load_materials(); !r) return r.error();
        if (auto r = load_meshes(); !r) return r.error();
        if (auto r = load_nodes(); !r) return r.error();
        if (doc_.find("animations") || doc_.find("skins")) model_.warnings.push_back("animations and skins are not imported yet");
        return std::move(model_);
    }

private:
    Result<void> load_buffers() {
        const auto* buffers = array_field(doc_, "buffers");
        if (!buffers) return ok();
        for (std::size_t i = 0; i < buffers->size(); ++i) {
            const Json& b = (*buffers)[i];
            const auto len = index_field(b, "byteLength", static_cast<std::size_t>(-1));
            if (!len || *len == static_cast<std::size_t>(-1)) return invalid("buffer " + std::to_string(i) + " needs byteLength");
            std::vector<std::uint8_t> data;
            const std::string uri = string_field(b, "uri");
            if (uri.empty()) {
                if (i != 0 || glb_bin_.empty()) return invalid("buffer " + std::to_string(i) + " has no data");
                data.assign(glb_bin_.begin(), glb_bin_.end());
            } else {
                auto loaded = load_uri(uri, base_dir_, opt_.max_file_bytes);
                if (!loaded) return loaded.error();
                data = std::move(loaded).value();
            }
            if (data.size() < *len) return invalid("buffer " + std::to_string(i) + " is shorter than byteLength");
            data.resize(*len);
            buffers_.push_back(std::move(data));
        }
        return ok();
    }

    Result<void> load_views() {
        const auto* views = array_field(doc_, "bufferViews");
        if (!views) return ok();
        for (std::size_t i = 0; i < views->size(); ++i) {
            const Json& v = (*views)[i];
            const auto buf = index_field(v, "buffer", static_cast<std::size_t>(-1));
            const auto off = index_field(v, "byteOffset", 0);
            const auto len = index_field(v, "byteLength", static_cast<std::size_t>(-1));
            const auto stride = index_field(v, "byteStride", 0);
            if (!buf || !off || !len || !stride || *buf >= buffers_.size() || *len == static_cast<std::size_t>(-1))
                return invalid("bufferView " + std::to_string(i) + " is malformed");
            const auto& b = buffers_[*buf];
            if (*off > b.size() || *len > b.size() - *off) return invalid("bufferView " + std::to_string(i) + " is out of bounds");
            if (*stride != 0 && (*stride < 4 || *stride > 252)) return invalid("bufferView " + std::to_string(i) + " has an invalid byteStride");
            views_.push_back(View{b.data() + *off, *len, *stride});
        }
        return ok();
    }

    Result<Accessor> accessor(std::size_t index, std::size_t want_components) {
        const auto* accessors = array_field(doc_, "accessors");
        if (!accessors || index >= accessors->size()) return invalid("accessor " + std::to_string(index) + " does not exist");
        const Json& a = (*accessors)[index];
        if (a.find("sparse")) return Error{ErrorCode::Unsupported, "glTF: sparse accessors are not supported"};
        Accessor out;
        const auto ctype = index_field(a, "componentType", 0);
        const auto count = index_field(a, "count", static_cast<std::size_t>(-1));
        const auto off = index_field(a, "byteOffset", 0);
        if (!ctype || !count || !off || *count == static_cast<std::size_t>(-1)) return invalid("accessor " + std::to_string(index) + " is malformed");
        out.component_type = static_cast<int>(*ctype);
        out.component_size = component_bytes(out.component_type);
        out.components = component_count(string_field(a, "type"));
        out.count = *count;
        if (const Json* n = a.find("normalized"); n && n->is_bool()) out.normalized = n->as_bool();
        if (out.component_size == 0 || out.components == 0) return invalid("accessor " + std::to_string(index) + " has an unknown type");
        if (want_components && out.components != want_components)
            return invalid("accessor " + std::to_string(index) + " has the wrong number of components");
        if (out.count > opt_.max_vertices * 3) return invalid("accessor " + std::to_string(index) + " is too large");
        const std::size_t elem = out.components * out.component_size;
        const Json* view_index = a.find("bufferView");
        if (!view_index) {
            out.stride = elem;  // zero-filled
            return out;
        }
        const auto vi = index_field(a, "bufferView", 0);
        if (!vi || *vi >= views_.size()) return invalid("accessor " + std::to_string(index) + " references a missing bufferView");
        const View& v = views_[*vi];
        out.stride = v.stride ? v.stride : elem;
        if (out.stride < elem) return invalid("accessor " + std::to_string(index) + " has a stride smaller than its element");
        if (out.count > 0) {
            // offset + stride * (count - 1) + elem <= view length, without overflow
            if (*off > v.length) return invalid("accessor " + std::to_string(index) + " is out of bounds");
            const std::size_t avail = v.length - *off;
            if (elem > avail || (out.count - 1) > (avail - elem) / out.stride)
                return invalid("accessor " + std::to_string(index) + " is out of bounds");
        }
        out.base = v.data + *off;
        return out;
    }

    static float read_component(const Accessor& a, const std::uint8_t* p) {
        switch (a.component_type) {
            case 5126: { float f; std::memcpy(&f, p, 4); return f; }
            case 5121: return a.normalized ? static_cast<float>(*p) / 255.0f : static_cast<float>(*p);
            case 5120: { std::int8_t v; std::memcpy(&v, p, 1); return a.normalized ? std::max(static_cast<float>(v) / 127.0f, -1.0f) : static_cast<float>(v); }
            case 5123: { std::uint16_t v; std::memcpy(&v, p, 2); return a.normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v); }
            case 5122: { std::int16_t v; std::memcpy(&v, p, 2); return a.normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f) : static_cast<float>(v); }
            case 5125: { std::uint32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v); }
            default: return 0;
        }
    }

    // Reads `count * components` floats; rejects non-finite values.
    Result<std::vector<float>> read_floats(const Accessor& a) {
        std::vector<float> out(a.count * a.components, 0.0f);
        if (!a.base) return out;
        for (std::size_t i = 0; i < a.count; ++i)
            for (std::size_t c = 0; c < a.components; ++c) {
                const float f = read_component(a, a.base + i * a.stride + c * a.component_size);
                if (!std::isfinite(f)) return invalid("non-finite vertex data");
                out[i * a.components + c] = f;
            }
        return out;
    }

    Result<void> load_textures() {
        const auto* textures = array_field(doc_, "textures");
        if (!textures) return ok();
        const auto* images = array_field(doc_, "images");
        for (std::size_t i = 0; i < textures->size(); ++i) {
            const auto src = index_field((*textures)[i], "source", static_cast<std::size_t>(-1));
            if (!src || !images || *src >= images->size()) {
                model_.warnings.push_back("texture " + std::to_string(i) + " has no supported image; using white");
                model_.textures.push_back(white());
                continue;
            }
            const Json& img = (*images)[*src];
            std::vector<std::uint8_t> bytes;
            if (const std::string uri = string_field(img, "uri"); !uri.empty()) {
                auto loaded = load_uri(uri, base_dir_, opt_.max_file_bytes);
                if (!loaded) return loaded.error();
                bytes = std::move(loaded).value();
            } else {
                const auto vi = index_field(img, "bufferView", static_cast<std::size_t>(-1));
                if (!vi || *vi >= views_.size()) return invalid("image " + std::to_string(*src) + " has no data");
                bytes.assign(views_[*vi].data, views_[*vi].data + views_[*vi].length);
            }
            auto decoded = decode_image(bytes, opt_.max_texture_size);
            if (!decoded) {
                model_.warnings.push_back("image " + std::to_string(*src) + ": " + decoded.error().message + "; using white");
                model_.textures.push_back(white());
                continue;
            }
            model_.textures.push_back(std::move(decoded).value());
        }
        return ok();
    }

    static TextureImage white() { return TextureImage{1, 1, {255, 255, 255, 255}}; }

    Result<void> load_materials() {
        const auto* materials = array_field(doc_, "materials");
        if (!materials) return ok();
        for (std::size_t i = 0; i < materials->size(); ++i) {
            const Json& m = (*materials)[i];
            Material mat;
            mat.name = string_field(m, "name");
            if (const Json* pbr = m.find("pbrMetallicRoughness"); pbr && pbr->is_object()) {
                if (!read_float_array(*pbr, "baseColorFactor", mat.base_color.data(), 4))
                    return invalid("material " + std::to_string(i) + " has an invalid baseColorFactor");
                if (const Json* t = pbr->find("baseColorTexture"); t && t->is_object()) {
                    const auto ti = index_field(*t, "index", static_cast<std::size_t>(-1));
                    if (ti && *ti < model_.textures.size()) mat.base_color_texture = static_cast<int>(*ti);
                    else model_.warnings.push_back("material " + std::to_string(i) + " references a missing texture");
                    if (index_field(*t, "texCoord", 0).value_or(0) != 0)
                        model_.warnings.push_back("material " + std::to_string(i) + " uses TEXCOORD_1; using TEXCOORD_0");
                }
            }
            model_.materials.push_back(std::move(mat));
        }
        return ok();
    }

    Result<void> load_meshes() {
        const auto* meshes = array_field(doc_, "meshes");
        if (!meshes) return ok();
        std::size_t total_vertices = 0;
        for (std::size_t mi = 0; mi < meshes->size(); ++mi) {
            const Json& m = (*meshes)[mi];
            Mesh mesh;
            mesh.name = string_field(m, "name");
            const auto* prims = array_field(m, "primitives");
            if (!prims) return invalid("mesh " + std::to_string(mi) + " has no primitives");
            for (std::size_t pi = 0; pi < prims->size(); ++pi) {
                const Json& p = (*prims)[pi];
                const std::string where = "mesh " + std::to_string(mi) + " primitive " + std::to_string(pi);
                if (index_field(p, "mode", 4).value_or(99) != 4) {
                    model_.warnings.push_back(where + " is not a triangle list; skipped");
                    mesh.primitives.emplace_back();  // keep primitive numbering stable
                    continue;
                }
                const Json* attrs = p.find("attributes");
                if (!attrs || !attrs->is_object()) return invalid(where + " has no attributes");
                const auto pos_i = index_field(*attrs, "POSITION", static_cast<std::size_t>(-1));
                if (!pos_i || *pos_i == static_cast<std::size_t>(-1)) return invalid(where + " has no POSITION");
                auto pos_acc = accessor(*pos_i, 3);
                if (!pos_acc) return pos_acc.error();
                auto pos = read_floats(pos_acc.value());
                if (!pos) return pos.error();
                const std::size_t n = pos_acc.value().count;
                total_vertices += n;
                if (total_vertices > opt_.max_vertices) return invalid("model exceeds the vertex limit");

                Primitive prim;
                prim.positions.resize(n);
                for (std::size_t i = 0; i < n; ++i) prim.positions[i] = {pos.value()[i * 3], pos.value()[i * 3 + 1], pos.value()[i * 3 + 2]};

                if (const auto ni = index_field(*attrs, "NORMAL", static_cast<std::size_t>(-1)); ni && *ni != static_cast<std::size_t>(-1)) {
                    auto acc = accessor(*ni, 3);
                    if (!acc) return acc.error();
                    if (acc.value().count != n) return invalid(where + " NORMAL count differs from POSITION");
                    auto f = read_floats(acc.value());
                    if (!f) return f.error();
                    prim.normals.resize(n);
                    for (std::size_t i = 0; i < n; ++i) prim.normals[i] = math::normalize(math::Vec3{f.value()[i * 3], f.value()[i * 3 + 1], f.value()[i * 3 + 2]});
                }
                prim.uvs.assign(n, {0.0f, 0.0f});
                if (const auto ui = index_field(*attrs, "TEXCOORD_0", static_cast<std::size_t>(-1)); ui && *ui != static_cast<std::size_t>(-1)) {
                    auto acc = accessor(*ui, 2);
                    if (!acc) return acc.error();
                    if (acc.value().count != n) return invalid(where + " TEXCOORD_0 count differs from POSITION");
                    auto f = read_floats(acc.value());
                    if (!f) return f.error();
                    for (std::size_t i = 0; i < n; ++i) prim.uvs[i] = {f.value()[i * 2], f.value()[i * 2 + 1]};
                }

                if (const auto ii = index_field(p, "indices", static_cast<std::size_t>(-1)); ii && *ii != static_cast<std::size_t>(-1)) {
                    auto acc = accessor(*ii, 1);
                    if (!acc) return acc.error();
                    const Accessor& a = acc.value();
                    if (a.component_type != 5121 && a.component_type != 5123 && a.component_type != 5125)
                        return invalid(where + " indices must be unsigned integers");
                    prim.indices.resize(a.count);
                    for (std::size_t i = 0; i < a.count; ++i) {
                        std::uint32_t v = 0;
                        if (a.base) {
                            const std::uint8_t* src = a.base + i * a.stride;
                            if (a.component_type == 5121) v = *src;
                            else if (a.component_type == 5123) { std::uint16_t s; std::memcpy(&s, src, 2); v = s; }
                            else std::memcpy(&v, src, 4);
                        }
                        if (v >= n) return invalid(where + " has an index out of range");
                        prim.indices[i] = v;
                    }
                } else {
                    prim.indices.resize(n);
                    for (std::size_t i = 0; i < n; ++i) prim.indices[i] = static_cast<std::uint32_t>(i);
                }
                if (prim.indices.size() % 3 != 0) return invalid(where + " index count is not a multiple of 3");
                if (prim.normals.empty()) compute_normals(prim);

                const auto mat = index_field(p, "material", static_cast<std::size_t>(-1));
                if (mat && *mat != static_cast<std::size_t>(-1)) {
                    if (*mat >= model_.materials.size()) return invalid(where + " references a missing material");
                    prim.material = static_cast<int>(*mat);
                }
                mesh.primitives.push_back(std::move(prim));
            }
            model_.meshes.push_back(std::move(mesh));
        }
        return ok();
    }

    // Area-weighted smooth normals, as the glTF spec suggests when absent.
    static void compute_normals(Primitive& p) {
        p.normals.assign(p.positions.size(), math::Vec3{0, 0, 0});
        for (std::size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            const auto a = p.indices[i], b = p.indices[i + 1], c = p.indices[i + 2];
            const math::Vec3 n = math::cross(p.positions[b] - p.positions[a], p.positions[c] - p.positions[a]);
            p.normals[a] = p.normals[a] + n;
            p.normals[b] = p.normals[b] + n;
            p.normals[c] = p.normals[c] + n;
        }
        for (auto& n : p.normals) n = math::length(n) > 0 ? math::normalize(n) : math::Vec3{0, 1, 0};
    }

    Result<void> load_nodes() {
        const auto* nodes = array_field(doc_, "nodes");
        if (!nodes) return ok();
        std::vector<int> parent(nodes->size(), -1);
        for (std::size_t i = 0; i < nodes->size(); ++i) {
            const Json& n = (*nodes)[i];
            Node node;
            node.name = string_field(n, "name");
            const auto mesh = index_field(n, "mesh", static_cast<std::size_t>(-1));
            if (mesh && *mesh != static_cast<std::size_t>(-1)) {
                if (*mesh >= model_.meshes.size()) return invalid("node " + std::to_string(i) + " references a missing mesh");
                node.mesh = static_cast<int>(*mesh);
            }
            float m[16];
            if (n.find("matrix")) {
                if (!read_float_array(n, "matrix", m, 16)) return invalid("node " + std::to_string(i) + " has an invalid matrix");
                math::Mat4 mat;
                std::memcpy(mat.m, m, sizeof m);  // glTF matrices are column-major, like Mat4
                math::decompose(mat, node.translation, node.rotation, node.scale);
            } else {
                float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
                if (!read_float_array(n, "translation", t, 3) || !read_float_array(n, "rotation", r, 4) ||
                    !read_float_array(n, "scale", s, 3))
                    return invalid("node " + std::to_string(i) + " has an invalid transform");
                node.translation = {t[0], t[1], t[2]};
                const math::Quat q{r[0], r[1], r[2], r[3]};
                node.rotation = (q.x == 0 && q.y == 0 && q.z == 0 && q.w == 0) ? math::Quat{} : math::normalize(q);
                node.scale = {s[0], s[1], s[2]};
            }
            if (const auto* kids = array_field(n, "children")) {
                for (const Json& k : *kids) {
                    if (!k.is_number() || k.as_number() < 0 || k.as_number() >= static_cast<double>(nodes->size()) ||
                        k.as_number() != std::floor(k.as_number()))
                        return invalid("node " + std::to_string(i) + " has an invalid child");
                    const auto c = static_cast<std::size_t>(k.as_number());
                    if (parent[c] != -1 || c == i) return invalid("node " + std::to_string(c) + " has more than one parent");
                    parent[c] = static_cast<int>(i);
                    node.children.push_back(static_cast<int>(c));
                }
            }
            model_.nodes.push_back(std::move(node));
        }
        // Each node has at most one parent; a cycle would leave a loop with no root.
        for (std::size_t i = 0; i < parent.size(); ++i) {
            std::size_t steps = 0;
            for (int p = parent[i]; p != -1; p = parent[static_cast<std::size_t>(p)])
                if (++steps > parent.size()) return invalid("node hierarchy contains a cycle");
        }

        const auto* scenes = array_field(doc_, "scenes");
        const auto scene = index_field(doc_, "scene", 0).value_or(0);
        if (scenes && scene < scenes->size()) {
            if (const auto* roots = array_field((*scenes)[scene], "nodes"))
                for (const Json& r : *roots) {
                    if (!r.is_number() || r.as_number() < 0 || r.as_number() >= static_cast<double>(nodes->size()))
                        return invalid("scene references a missing node");
                    const int idx = static_cast<int>(r.as_number());
                    if (parent[static_cast<std::size_t>(idx)] != -1) return invalid("scene root " + std::to_string(idx) + " has a parent");
                    model_.roots.push_back(idx);
                }
        } else {
            for (std::size_t i = 0; i < parent.size(); ++i)
                if (parent[i] == -1) model_.roots.push_back(static_cast<int>(i));
        }
        return ok();
    }

    const Json& doc_;
    fs::path base_dir_;
    ImportOptions opt_;
    std::span<const std::uint8_t> glb_bin_;
    std::vector<std::vector<std::uint8_t>> buffers_;
    std::vector<View> views_;
    Model model_;
};

std::uint32_t read_u32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;  // GLB is little-endian; so are all supported targets
}

}  // namespace

Result<std::vector<std::uint8_t>> decode_base64(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t acc = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') { ++padding; continue; }
        else return Error{ErrorCode::InvalidArgument, "invalid base64 character"};
        if (padding) return Error{ErrorCode::InvalidArgument, "base64 data after padding"};
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    if (padding > 2) return Error{ErrorCode::InvalidArgument, "invalid base64 padding"};
    return out;
}

Result<Model> parse_gltf(std::string_view json, const fs::path& base_dir, const ImportOptions& options) {
    auto doc = parse_json(json);
    if (!doc) return Error{ErrorCode::InvalidArgument, "glTF: " + doc.error().message};
    return Importer(doc.value(), base_dir, options, {}).run();
}

Result<Model> parse_glb(std::span<const std::uint8_t> bytes, const fs::path& base_dir, const ImportOptions& options) {
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0) return invalid("not a GLB file");
    if (read_u32(bytes.data() + 4) != 2) return invalid("unsupported GLB version");
    const std::size_t total = read_u32(bytes.data() + 8);
    if (total > bytes.size() || total < 20) return invalid("GLB length is inconsistent");
    std::size_t pos = 12;
    std::string_view json;
    std::span<const std::uint8_t> bin;
    while (pos + 8 <= total) {
        const std::size_t len = read_u32(bytes.data() + pos);
        const std::uint32_t type = read_u32(bytes.data() + pos + 4);
        pos += 8;
        if (len > total - pos) return invalid("GLB chunk is out of bounds");
        if (type == 0x4E4F534Au && json.empty()) json = std::string_view(reinterpret_cast<const char*>(bytes.data() + pos), len);
        else if (type == 0x004E4942u && bin.empty()) bin = bytes.subspan(pos, len);
        pos += len;
    }
    if (json.empty()) return invalid("GLB has no JSON chunk");
    auto doc = parse_json(json);
    if (!doc) return Error{ErrorCode::InvalidArgument, "glTF: " + doc.error().message};
    return Importer(doc.value(), base_dir, options, bin).run();
}

Result<Model> load_gltf_file(const fs::path& path, const ImportOptions& options) {
    auto bytes = read_file(path, options.max_file_bytes);
    if (!bytes) return bytes.error();
    const fs::path base = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    const auto& b = bytes.value();
    Result<Model> model = (b.size() >= 4 && std::memcmp(b.data(), "glTF", 4) == 0)
                              ? parse_glb(b, base, options)
                              : parse_gltf(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()), base, options);
    if (!model) return Error{model.error().code, path.filename().string() + ": " + model.error().message};
    return model;
}

}  // namespace aether::assets
