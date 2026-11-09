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

AETHER_TEST(matrix_inverse_round_trips) {
    const Mat4 m = Mat4::from_trs({3, -2, 7}, normalize(Quat::from_axis_angle({1, 2, 3}, 0.8f)), {2, 0.5f, 3});
    Mat4 inv;
    REQUIRE(inverse(m, inv));
    CHECK(near(m * inv, Mat4::identity(), 1e-4f));
    CHECK(near(inv * m, Mat4::identity(), 1e-4f));
    Mat4 singular = Mat4::from_trs({}, Quat::identity(), {1, 0, 1});
    Mat4 untouched;
    CHECK(!inverse(singular, untouched));
}

AETHER_TEST(decompose_recovers_trs) {
    const Vec3 t{1, 2, 3}, s{2, 3, 4};
    const Quat r = normalize(Quat::from_axis_angle({0.3f, 1, -0.2f}, 1.2f));
    Vec3 t2, s2;
    Quat r2;
    decompose(Mat4::from_trs(t, r, s), t2, r2, s2);
    CHECK(near(t2, t));
    CHECK(near(s2, s, 1e-4f));
    CHECK(near(Mat4::from_trs(t2, r2, s2), Mat4::from_trs(t, r, s), 1e-4f));
    // Mirrored transform keeps its handedness after a round trip.
    decompose(Mat4::from_trs(t, r, {-1, 1, 1}), t2, r2, s2);
    CHECK(near(Mat4::from_trs(t2, r2, s2), Mat4::from_trs(t, r, {-1, 1, 1}), 1e-4f));
}

AETHER_TEST(euler_degrees_round_trip) {
    for (const Vec3 e : {Vec3{0, 0, 0}, Vec3{30, 45, 60}, Vec3{-20, 170, -90}, Vec3{89, -45, 10}}) {
        const Quat q = quat_from_euler_degrees(e);
        const Vec3 back = euler_degrees_from_quat(q);
        CHECK(near(Mat4::from_trs({}, quat_from_euler_degrees(back), {1, 1, 1}),
                   Mat4::from_trs({}, q, {1, 1, 1}), 1e-4f));
    }
    // Yaw of 90 degrees turns +Z (forward) toward +X.
    CHECK(near(rotate(quat_from_euler_degrees({0, 90, 0}), {0, 0, 1}), Vec3{1, 0, 0}, 1e-5f));
    CHECK(near(euler_degrees_from_quat(quat_from_euler_degrees({10, 20, 30})), Vec3{10, 20, 30}, 1e-3f));
    // Gimbal lock still yields an equivalent rotation.
    const Quat locked = quat_from_euler_degrees({90, 30, 20});
    CHECK(near(Mat4::from_trs({}, quat_from_euler_degrees(euler_degrees_from_quat(locked)), {1, 1, 1}),
               Mat4::from_trs({}, locked, {1, 1, 1}), 1e-3f));
}
