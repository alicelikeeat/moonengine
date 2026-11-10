// Rigid body physics.
//
// Two components describe a physical object:
//  - Collider: its shape (box, sphere or capsule), sized in the entity's
//    local space and scaled by the entity's world scale.
//  - RigidBody: how it moves. Dynamic bodies are moved by forces and
//    collisions; kinematic bodies follow their Transform and push dynamic
//    bodies; static bodies never move. A Collider without a RigidBody is
//    static.
//
// A PhysicsWorld simulates them. step() mirrors the ECS world into the
// simulation (new, changed and removed bodies; transforms edited by other
// code), advances one fixed step and writes the results back to Transform
// and RigidBody velocities. The backend is Jolt Physics (ADR 0015); no Jolt
// type appears in this header.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "aether/foundation/log.hpp"
#include "aether/foundation/math.hpp"
#include "aether/foundation/result.hpp"
#include "aether/world/serialization.hpp"
#include "aether/world/world.hpp"

namespace aether::simulation {

enum class BodyType : std::uint8_t { Static, Dynamic, Kinematic };

struct RigidBody {
    static constexpr std::string_view kComponentName = "RigidBody";
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;             // kilograms, dynamic bodies
    float friction = 0.5f;         // 0 = ice, 1 = rubber
    float restitution = 0.1f;      // bounciness, 0..1; a contact uses the larger of the two bodies' values
    float linear_damping = 0.05f;  // fraction of velocity lost per second
    float angular_damping = 0.05f;
    float gravity_scale = 1.0f;
    // World space. Written by step(); changing them between steps sets the
    // body's velocity (their values when the body is created are its
    // initial velocity).
    math::Vec3 linear_velocity{0, 0, 0};   // metres per second
    math::Vec3 angular_velocity{0, 0, 0};  // radians per second
};

enum class ShapeType : std::uint8_t { Box, Sphere, Capsule };

struct Collider {
    static constexpr std::string_view kComponentName = "Collider";
    ShapeType shape = ShapeType::Box;
    math::Vec3 half_extents{0.5f, 0.5f, 0.5f};  // box
    float radius = 0.5f;                         // sphere, capsule
    float half_height = 0.5f;                    // capsule: half the cylinder part, along Y
    math::Vec3 offset{0, 0, 0};                  // centre, in the entity's local space

    bool operator==(const Collider&) const noexcept = default;
};

struct PhysicsDesc {
    math::Vec3 gravity{0, -9.81f, 0};
    std::uint32_t max_bodies = 16384;
    Logger* logger = nullptr;
};

struct RayHit {
    world::Entity entity;
    float distance = 0.0f;
    math::Vec3 point;
    math::Vec3 normal;  // unit, pointing away from the surface
};

struct StepStats {
    std::uint32_t bodies = 0;         // bodies in the simulation
    std::uint32_t active_bodies = 0;  // not asleep
    std::uint32_t created = 0;        // bodies created or rebuilt this step
    std::uint32_t removed = 0;
};

class PhysicsWorld {
public:
    static Result<std::unique_ptr<PhysicsWorld>> create(const PhysicsDesc& desc = {});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Advances the simulation by `dt` seconds (call with a fixed step, e.g.
    // 1/60). Runs propagate_transforms() before reading the world and after
    // writing results. Dynamic bodies should not be children of other
    // dynamic bodies: a child follows its parent and the simulation.
    StepStats step(world::World& world, float dt);

    // Nearest collider along the ray, if any within `max_distance`.
    [[nodiscard]] std::optional<RayHit> raycast(math::Vec3 origin, math::Vec3 direction, float max_distance) const;

    // Instant change of velocity for a dynamic body (newton-seconds at its
    // centre of mass). Returns false if the entity has no dynamic body yet.
    bool add_impulse(world::Entity entity, math::Vec3 impulse);

    void set_gravity(math::Vec3 gravity);
    [[nodiscard]] math::Vec3 gravity() const;
    [[nodiscard]] std::uint32_t body_count() const;

private:
    PhysicsWorld();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Level-file support:
//   RigidBody: {"type": "static" | "dynamic" | "kinematic", "mass": kg,
//               "friction": f, "restitution": r, "linear_damping": d,
//               "angular_damping": d, "gravity_scale": g,
//               "linear_velocity": [x, y, z], "angular_velocity": [x, y, z]}
//   Collider:  {"shape": "box" | "sphere" | "capsule", "half_extents": [x, y, z],
//               "radius": r, "half_height": h, "offset": [x, y, z]}
// Only the fields a shape uses are written; velocities only when non-zero.
void register_components(world::SerializerRegistry& registry);

}  // namespace aether::simulation
