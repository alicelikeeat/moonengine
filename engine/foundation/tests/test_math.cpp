#include "aether/foundation/math.hpp"
#include "aether_test.hpp"

using namespace aether::math;

AETHER_TEST(vec3_basics) {
    constexpr Vec3 a{1, 2, 3}, b{4, 5, 6};
    static_assert(dot(a, b) == 32.0f);
    static_assert(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1});  // right-handed
    CHECK(near(length(Vec3{3, 4, 0}), 5.0f));
    CHECK(near(length(normalize(Vec3{10, -2, 7})), 1.0f));
    CHECK(normalize(Vec3{}) == Vec3{});
}

AETHER_TEST(quat_rotates_vectors) {
    const Quat z90 = Quat::from_axis_angle({0, 0, 1}, kPi / 2);
    CHECK(near(rotate(z90, {1, 0, 0}), Vec3{0, 1, 0}));
    const Quat y90 = Quat::from_axis_angle({0, 1, 0}, kPi / 2);
    CHECK(near(rotate(y90, {1, 0, 0}), Vec3{0, 0, -1}));
    CHECK(near(rotate(Quat::identity(), {1, 2, 3}), Vec3{1, 2, 3}));
}

AETHER_TEST(quat_composition_applies_right_first) {
    const Quat a = Quat::from_axis_angle({0, 0, 1}, kPi / 2);
    const Quat b = Quat::from_axis_angle({1, 0, 0}, kPi / 2);
    const Vec3 v{0, 1, 0};
    CHECK(near(rotate(a * b, v), rotate(a, rotate(b, v))));
}

AETHER_TEST(trs_matrix_matches_manual_composition) {
    const Vec3 t{10, -2, 5}, s{2, 3, 0.5f};
    const Quat r = normalize(Quat::from_axis_angle({1, 1, 0}, 0.7f));
    const Mat4 m = Mat4::from_trs(t, r, s);
    for (const Vec3 p : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{-3, 4, 2}}) {
        const Vec3 expected = rotate(r, p * s) + t;
        CHECK(near(m.transform_point(p), expected, 1e-4f));
    }
    CHECK(near(m.translation(), t));
}

AETHER_TEST(matrix_product_composes_transforms) {
    const Mat4 a = Mat4::from_trs({1, 2, 3}, Quat::from_axis_angle({0, 1, 0}, 0.3f), {1, 1, 1});
    const Mat4 b = Mat4::from_trs({-4, 0, 1}, Quat::from_axis_angle({1, 0, 0}, 1.1f), {2, 2, 2});
    const Vec3 p{0.5f, -1, 2};
    CHECK(near((a * b).transform_point(p), a.transform_point(b.transform_point(p)), 1e-4f));
    CHECK(near(Mat4::identity() * a, a));
    CHECK(near(a * Mat4::identity(), a));
}
