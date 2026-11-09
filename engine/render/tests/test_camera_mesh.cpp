#include <cmath>

#include "aether/render/camera.hpp"
#include "aether/render/mesh.hpp"
#include "aether_test.hpp"

using namespace aether::math;
using namespace aether::render;

AETHER_TEST(cube_has_outward_ccw_faces) {
    const MeshData cube = make_cube(2.0f);
    CHECK_EQ(cube.vertices.size(), std::size_t{24});
    REQUIRE(cube.indices.size() == 36);
    for (const Vertex& v : cube.vertices) {
        CHECK(near(length(v.normal), 1.0f));
        CHECK(near(dot(v.position, v.normal), 1.0f));  // on its face plane, half-size 1
        CHECK(std::fabs(v.position.x) <= 1.0f && std::fabs(v.position.y) <= 1.0f && std::fabs(v.position.z) <= 1.0f);
    }
    for (std::size_t i = 0; i < cube.indices.size(); i += 3) {
        const Vertex& a = cube.vertices[cube.indices[i]];
        const Vertex& b = cube.vertices[cube.indices[i + 1]];
        const Vertex& c = cube.vertices[cube.indices[i + 2]];
        // Counter-clockwise seen from outside == geometric normal points out.
        CHECK(dot(cross(b.position - a.position, c.position - a.position), a.normal) > 0);
    }
}

AETHER_TEST(projection_maps_world_axes_to_screen) {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    const Mat4 vp = view_projection(cam, 16.0f / 9.0f);
    const Vec3 center = project_to_ndc(vp, {0, 0, 0});
    CHECK(near(center.x, 0) && near(center.y, 0));
    CHECK(project_to_ndc(vp, {0, 1, 0}).y < 0);  // world up -> top of screen (NDC y down)
    CHECK(project_to_ndc(vp, {1, 0, 0}).x > 0);  // world +X -> right
    const float z_near = project_to_ndc(vp, {0, 0, 5 - cam.near_plane}).z;
    const float z_far = project_to_ndc(vp, {0, 0, 5 - cam.far_plane}).z;
    CHECK(near(z_near, 0.0f, 1e-4f));
    CHECK(near(z_far, 1.0f, 1e-4f));
    CHECK(project_to_ndc(vp, {0, 0, -1}).z > project_to_ndc(vp, {0, 0, 1}).z);  // farther = deeper
}

AETHER_TEST(look_at_moves_eye_to_origin) {
    const Mat4 view = look_at({3, 4, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(near(view.transform_point({3, 4, 5}), Vec3{0, 0, 0}, 1e-5f));
    const Vec3 target_in_view = view.transform_point({0, 0, 0});
    CHECK(target_in_view.z < 0);  // camera looks down -Z
    CHECK(near(target_in_view.x, 0) && near(target_in_view.y, 0));
}
