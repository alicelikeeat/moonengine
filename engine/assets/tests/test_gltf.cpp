#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "aether/assets/model.hpp"
#include "aether/foundation/json.hpp"
#include "aether_test.hpp"

using namespace aether;
using namespace aether::assets;
namespace fs = std::filesystem;

namespace {

// 2x2 RGBA PNG: red, green / blue, half-transparent white.
const std::vector<std::uint8_t> kPng = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x19, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x05, 0xc1, 0x01, 0x0d, 0x00, 0x00, 0x08, 0xc0, 0x20, 0xdc, 0x2c, 0x6e, 0xf2, 0x0b, 0x22, 0x69, 0x47, 0xe2, 0x1e, 0x3f, 0x80, 0x06, 0x81, 0x35, 0xb4, 0x56, 0x15, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

std::string base64(const std::vector<std::uint8_t>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        const std::uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == d.size()) {
        const std::uint32_t v = d[i] << 16;
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == d.size()) {
        const std::uint32_t v = (d[i] << 16) | (d[i + 1] << 8);
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

// Minimal glTF builder: one buffer, views and accessors appended in order.
struct Builder {
    std::vector<std::uint8_t> bin;
    Json views = Json::array(), accessors = Json::array();

    template <typename T>
    std::size_t add(const std::vector<T>& values, int component_type, const char* type, std::size_t count) {
        while (bin.size() % 4) bin.push_back(0);
        const std::size_t offset = bin.size();
        const auto* p = reinterpret_cast<const std::uint8_t*>(values.data());
        bin.insert(bin.end(), p, p + values.size() * sizeof(T));
        Json v = Json::object();
        v.set("buffer", 0);
        v.set("byteOffset", static_cast<std::uint64_t>(offset));
        v.set("byteLength", static_cast<std::uint64_t>(values.size() * sizeof(T)));
        views.push_back(v);
        Json a = Json::object();
        a.set("bufferView", static_cast<std::uint64_t>(views.as_array().size() - 1));
        a.set("componentType", component_type);
        a.set("count", static_cast<std::uint64_t>(count));
        a.set("type", type);
        accessors.push_back(a);
        return accessors.as_array().size() - 1;
    }

    // A unit quad in XY with uvs and u16 indices.
    Json quad_document() {
        const std::vector<float> pos = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
        const std::vector<float> uv = {0, 1, 1, 1, 1, 0, 0, 0};
        const std::vector<std::uint16_t> idx = {0, 1, 2, 0, 2, 3};
        const auto p = add(pos, 5126, "VEC3", 4);
        const auto u = add(uv, 5126, "VEC2", 4);
        const auto i = add(idx, 5123, "SCALAR", 6);
        Json attrs = Json::object();
        attrs.set("POSITION", static_cast<std::uint64_t>(p));
        attrs.set("TEXCOORD_0", static_cast<std::uint64_t>(u));
        Json prim = Json::object();
        prim.set("attributes", attrs);
        prim.set("indices", static_cast<std::uint64_t>(i));
        prim.set("material", 0);
        Json prims = Json::array();
        prims.push_back(prim);
        Json mesh = Json::object();
        mesh.set("name", "Quad");
        mesh.set("primitives", prims);
        Json doc = Json::object();
        Json asset = Json::object();
        asset.set("version", "2.0");
        doc.set("asset", asset);
        Json meshes = Json::array();
        meshes.push_back(mesh);
        doc.set("meshes", meshes);
        doc.set("bufferViews", views);
        doc.set("accessors", accessors);
        auto mat = parse_json(R"({"name": "Paint", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 1, 1]}})").value();
        Json mats = Json::array();
        mats.push_back(mat);
        doc.set("materials", mats);
        doc.set("nodes", parse_json(R"([{"name": "Root", "translation": [1, 2, 3], "children": [1]},
                                        {"name": "Child", "mesh": 0, "scale": [2, 2, 2]}])").value());
        doc.set("scenes", parse_json(R"([{"nodes": [0]}])").value());
        return doc;
    }

    std::string as_gltf_with_data_uri(Json doc) {
        Json buf = Json::object();
        buf.set("byteLength", static_cast<std::uint64_t>(bin.size()));
        buf.set("uri", "data:application/octet-stream;base64," + base64(bin));
        Json bufs = Json::array();
        bufs.push_back(buf);
        doc.set("buffers", bufs);
        return to_json_string(doc);
    }

    std::vector<std::uint8_t> as_glb(Json doc) {
        Json buf = Json::object();
        buf.set("byteLength", static_cast<std::uint64_t>(bin.size()));
        Json bufs = Json::array();
        bufs.push_back(buf);
        doc.set("buffers", bufs);
        std::string json = to_json_string(doc);
        while (json.size() % 4) json += ' ';
        std::vector<std::uint8_t> b = bin;
        while (b.size() % 4) b.push_back(0);
        std::vector<std::uint8_t> out;
        const auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k))); };
        out.insert(out.end(), {'g', 'l', 'T', 'F'});
        u32(2);
        u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + b.size()));
        u32(static_cast<std::uint32_t>(json.size()));
        u32(0x4E4F534Au);
        out.insert(out.end(), json.begin(), json.end());
        u32(static_cast<std::uint32_t>(b.size()));
        u32(0x004E4942u);
        out.insert(out.end(), b.begin(), b.end());
        return out;
    }
};

}  // namespace

AETHER_TEST(base64_decodes_standard_and_url_alphabets) {
    CHECK(decode_base64("aGVsbG8=").value() == (std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'}));
    CHECK(decode_base64("-_8=").value() == (std::vector<std::uint8_t>{0xfb, 0xff}));
    CHECK(!decode_base64("ab!c").has_value());
    CHECK(!decode_base64("ab=c").has_value());
}

AETHER_TEST(png_decodes_to_rgba) {
    auto img = decode_image(kPng);
    REQUIRE(img.has_value());
    CHECK_EQ(img.value().width, 2u);
    CHECK_EQ(img.value().height, 2u);
    const auto& p = img.value().rgba;
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);       // top-left red
    CHECK(p[4] == 0 && p[5] == 255 && p[6] == 0);       // top-right green
    CHECK(p[8] == 0 && p[9] == 0 && p[10] == 255);      // bottom-left blue
    CHECK(p[15] == 128);                                // alpha kept
    CHECK(!decode_image(kPng, 1).has_value());          // over the size limit
    std::vector<std::uint8_t> garbage(64, 0x42);
    CHECK(!decode_image(garbage).has_value());
}

AETHER_TEST(gltf_with_data_uri_loads_mesh_material_nodes) {
    Builder b;
    auto model = parse_gltf(b.as_gltf_with_data_uri(b.quad_document()), {});
    REQUIRE(model.has_value());
    const Model& m = model.value();
    REQUIRE(m.meshes.size() == 1);
    REQUIRE(m.meshes[0].primitives.size() == 1);
    const Primitive& p = m.meshes[0].primitives[0];
    CHECK_EQ(p.positions.size(), std::size_t{4});
    CHECK(p.indices == (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
    CHECK(p.uvs[0][1] == 1.0f);
    // No NORMAL attribute: computed from the triangles (+Z for this quad).
    CHECK(math::near(p.normals[0], math::Vec3{0, 0, 1}));
    CHECK_EQ(p.material, 0);
    CHECK(m.materials[0].base_color[1] == 0.25f);
    // glTF defaults: fully metallic and rough unless the factors say otherwise.
    CHECK(m.materials[0].metallic == 1.0f && m.materials[0].roughness == 1.0f);
    CHECK_EQ(m.materials[0].normal_texture, -1);
    // Generated tangent: +X follows u; bitangent cross(N, T) * w = +Y points
    // up the image (v decreases with y on this quad).
    REQUIRE(p.tangents.size() == 4u);
    for (const auto& t : p.tangents) CHECK(t[0] > 0.999f && std::abs(t[1]) < 1e-5f && t[3] == 1.0f);
    REQUIRE(m.nodes.size() == 2);
    CHECK(m.roots == std::vector<int>{0});
    CHECK(m.nodes[0].children == std::vector<int>{1});
    CHECK(m.nodes[0].translation == (math::Vec3{1, 2, 3}));
    CHECK(m.nodes[1].scale == (math::Vec3{2, 2, 2}));
    CHECK_EQ(m.nodes[1].mesh, 0);
}

AETHER_TEST(glb_with_embedded_texture_and_matrix_node) {
    Builder b;
    Json doc = b.quad_document();
    // Embed the PNG in the binary chunk and reference it from the material.
    while (b.bin.size() % 4) b.bin.push_back(0);
    const std::size_t png_offset = b.bin.size();
    b.bin.insert(b.bin.end(), kPng.begin(), kPng.end());
    Json view = Json::object();
    view.set("buffer", 0);
    view.set("byteOffset", static_cast<std::uint64_t>(png_offset));
    view.set("byteLength", static_cast<std::uint64_t>(kPng.size()));
    b.views.push_back(view);
    doc.set("bufferViews", b.views);
    Json image = Json::object();
    image.set("bufferView", static_cast<std::uint64_t>(b.views.as_array().size() - 1));
    image.set("mimeType", "image/png");
    Json images = Json::array();
    images.push_back(image);
    doc.set("images", images);
    doc.set("textures", parse_json(R"([{"source": 0}])").value());
    doc.set("materials", parse_json(R"([{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}])").value());
    // Child uses a matrix: translate (4, 5, 6), scale 3.
    doc.set("nodes", parse_json(R"([{"children": [1]},
        {"mesh": 0, "matrix": [3,0,0,0, 0,3,0,0, 0,0,3,0, 4,5,6,1]}])").value());
    auto model = parse_glb(b.as_glb(doc), {});
    REQUIRE(model.has_value());
    const Model& m = model.value();
    REQUIRE(m.textures.size() == 1);
    CHECK_EQ(m.textures[0].width, 2u);
    CHECK_EQ(m.materials[0].base_color_texture, 0);
    CHECK(math::near(m.nodes[1].translation, math::Vec3{4, 5, 6}));
    CHECK(math::near(m.nodes[1].scale, math::Vec3{3, 3, 3}, 1e-5f));
    CHECK(m.roots == std::vector<int>{0});  // no scenes: parentless nodes are roots
}

AETHER_TEST(gltf_external_buffer_and_path_safety) {
    const fs::path dir = fs::temp_directory_path() / "aether_gltf_test";
    fs::create_directories(dir);
    Builder b;
    Json doc = b.quad_document();
    {
        std::ofstream out(dir / "quad data.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(b.bin.data()), static_cast<std::streamsize>(b.bin.size()));
    }
    Json buf = Json::object();
    buf.set("byteLength", static_cast<std::uint64_t>(b.bin.size()));
    buf.set("uri", "quad%20data.bin");
    Json bufs = Json::array();
    bufs.push_back(buf);
    doc.set("buffers", bufs);
    {
        std::ofstream out(dir / "quad.gltf");
        out << to_json_string(doc);
    }
    auto ok_model = load_gltf_file(dir / "quad.gltf");
    REQUIRE(ok_model.has_value());
    CHECK_EQ(ok_model.value().meshes.size(), std::size_t{1});

    for (const char* uri : {"../secret.bin", "/etc/passwd", "file:///etc/passwd", "http://example.com/a.bin", "sub/../../x.bin"}) {
        Json bad = doc;
        Json bb = Json::object();
        bb.set("byteLength", 4);
        bb.set("uri", uri);
        Json list = Json::array();
        list.push_back(bb);
        bad.set("buffers", list);
        auto r = parse_gltf(to_json_string(bad), dir);
        if (r) std::printf("    unexpectedly accepted uri %s\n", uri);
        CHECK(!r.has_value());
    }
    CHECK(!parse_gltf(to_json_string(doc), {}).has_value());  // external files forbidden without base dir
    fs::remove_all(dir);
}

AETHER_TEST(gltf_rejects_malformed_models) {
    const auto mutate = [](const std::function<void(Json&)>& f) {
        Builder b;
        Json doc = b.quad_document();
        f(doc);
        return parse_gltf(b.as_gltf_with_data_uri(doc), {});
    };
    // Accessor reads past its bufferView.
    CHECK(!mutate([](Json& d) { d.as_object()[3].value.as_array()[0].set("count", 400); }).has_value());
    // Index pointing past the vertices.
    CHECK(!mutate([](Json& d) { d.set("accessors", d.find("accessors")->as_array()); d.as_object()[3].value.as_array()[0].set("count", 5); }).has_value());
    // Node cycle.
    CHECK(!mutate([](Json& d) { d.set("nodes", parse_json(R"([{"children": [1]}, {"children": [0]}])").value()); d.set("scenes", Json::array()); }).has_value());
    // Two parents.
    CHECK(!mutate([](Json& d) { d.set("nodes", parse_json(R"([{"children": [2]}, {"children": [2]}, {}])").value()); }).has_value());
    // Wrong version, required extension, sparse accessor, missing material.
    CHECK(!mutate([](Json& d) { d.set("asset", parse_json(R"({"version": "1.0"})").value()); }).has_value());
    CHECK(!mutate([](Json& d) { d.set("extensionsRequired", parse_json(R"(["KHR_draco_mesh_compression"])").value()); }).has_value());
    CHECK(!mutate([](Json& d) { auto acc = d.find("accessors")->as_array(); acc[0].set("sparse", Json::object()); d.set("accessors", acc); }).has_value());
    CHECK(!mutate([](Json& d) { d.set("materials", Json::array()); }).has_value());
    CHECK(!parse_glb(std::vector<std::uint8_t>{'g', 'l', 'T', 'F', 2, 0, 0, 0}, {}).has_value());
}

AETHER_TEST(glb_fuzz_never_crashes) {
    Builder b;
    const auto seed = b.as_glb(b.quad_document());
    std::mt19937 rng(9);
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        auto bytes = seed;
        for (int k = 0; k < 1 + static_cast<int>(rng() % 4); ++k) {
            const std::size_t pos = rng() % bytes.size();
            switch (rng() % 3) {
                case 0: bytes[pos] = static_cast<std::uint8_t>(rng()); break;
                case 1: bytes[pos] ^= static_cast<std::uint8_t>(1u << (rng() % 8)); break;
                default: bytes.resize(pos + 1); break;
            }
        }
        auto r = parse_glb(bytes, {});
        if (r) {
            ++accepted;
            for (const auto& mesh : r.value().meshes)
                for (const auto& p : mesh.primitives)
                    for (const auto idx : p.indices) CHECK(idx < p.positions.size());
        }
    }
    std::printf("    %d of 3000 mutated GLB files were accepted (all with valid indices)\n", accepted);
}

AETHER_TEST(gltf_reads_pbr_material_channels_and_tangents) {
    Builder b;
    Json doc = b.quad_document();
    // Supplied tangents (unnormalised, negative handedness) need supplied normals.
    const auto nrm = b.add(std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}, 5126, "VEC3", 4);
    const auto tan = b.add(std::vector<float>{0, 2, 0, -1, 0, 2, 0, -1, 0, 2, 0, -1, 0, 2, 0, -1}, 5126, "VEC4", 4);
    doc.set("bufferViews", b.views);
    doc.set("accessors", b.accessors);
    Json& attrs = const_cast<Json&>(*doc.find("meshes")->as_array()[0].find("primitives")->as_array()[0].find("attributes"));
    attrs.set("NORMAL", static_cast<std::uint64_t>(nrm));
    attrs.set("TANGENT", static_cast<std::uint64_t>(tan));
    doc.set("images", parse_json(R"([{"uri": "data:image/png;base64,)" + base64(kPng) + R"("}])").value());
    doc.set("textures", parse_json(R"([{"source": 0}, {"source": 0}, {"source": 0}])").value());
    doc.set("materials", parse_json(R"([{"name": "Steel",
        "pbrMetallicRoughness": {"metallicFactor": 0.75, "roughnessFactor": 0.3, "baseColorTexture": {"index": 0},
                                 "metallicRoughnessTexture": {"index": 1}},
        "normalTexture": {"index": 2, "scale": 0.5},
        "occlusionTexture": {"index": 0},
        "emissiveFactor": [1, 0.5, 0]}])").value());
    auto model = parse_gltf(b.as_gltf_with_data_uri(doc), {});
    if (!model) std::fprintf(stderr, "    %s\n", model.error().message.c_str());
    REQUIRE(model.has_value());
    const Material& mat = model.value().materials[0];
    CHECK(mat.metallic == 0.75f && mat.roughness == 0.3f);
    CHECK_EQ(mat.base_color_texture, 0);
    CHECK_EQ(mat.metallic_roughness_texture, 1);
    CHECK_EQ(mat.normal_texture, 2);
    CHECK(mat.normal_scale == 0.5f);
    CHECK(mat.emissive == (math::Vec3{1, 0.5f, 0}));
    CHECK_EQ(model.value().warnings.size(), std::size_t{1});  // occlusion ignored
    const auto& t = model.value().meshes[0].primitives[0].tangents;
    REQUIRE(t.size() == 4u);
    CHECK(std::abs(t[0][1] - 1.0f) < 1e-6f && t[0][3] == -1.0f);

    // Out-of-range factors are clamped, malformed ones rejected.
    doc.set("materials", parse_json(R"([{"pbrMetallicRoughness": {"metallicFactor": 3, "roughnessFactor": -1}}])").value());
    auto clamped = parse_gltf(b.as_gltf_with_data_uri(doc), {});
    REQUIRE(clamped.has_value());
    CHECK(clamped.value().materials[0].metallic == 1.0f && clamped.value().materials[0].roughness == 0.0f);
    doc.set("materials", parse_json(R"([{"pbrMetallicRoughness": {"roughnessFactor": "rough"}}])").value());
    CHECK(!parse_gltf(b.as_gltf_with_data_uri(doc), {}));
}

AETHER_TEST(generated_tangents_follow_uvs_and_survive_degenerate_input) {
    Primitive p;
    // Quad in XZ facing +Y, u along -Z, v along -X (rotated mapping).
    p.positions = {{0, 0, 0}, {0, 0, -1}, {-1, 0, -1}, {-1, 0, 0}};
    p.normals.assign(4, math::Vec3{0, 1, 0});
    p.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    p.indices = {0, 1, 2, 0, 2, 3};
    generate_tangents(p);
    REQUIRE(p.tangents.size() == 4u);
    for (const auto& t : p.tangents) {
        CHECK(std::abs(t[2] + 1.0f) < 1e-5f);  // T = -Z
        // Image up (decreasing v) is +X here: cross(+Y, -Z) = -X, so w = -1.
        CHECK(t[3] == -1.0f);
    }
    // All UVs equal: still unit, perpendicular tangents.
    p.uvs.assign(4, {0.5f, 0.5f});
    generate_tangents(p);
    for (const auto& t : p.tangents) {
        const math::Vec3 v{t[0], t[1], t[2]};
        CHECK(std::abs(math::length(v) - 1.0f) < 1e-5f);
        CHECK(std::abs(math::dot(v, math::Vec3{0, 1, 0})) < 1e-5f);
    }
}
