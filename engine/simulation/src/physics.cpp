#include "aether/simulation/physics.hpp"

// Jolt.h must come first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "aether/world/hierarchy.hpp"

namespace aether::simulation {

namespace {

using math::Quat;
using math::Vec3;

// ---- Jolt process-wide state --------------------------------------------------
// Jolt keeps its type registry in globals; they are created with the first
// PhysicsWorld and destroyed with the last.
std::mutex g_jolt_mutex;
int g_jolt_users = 0;

void acquire_jolt() {
    const std::lock_guard lock(g_jolt_mutex);
    if (g_jolt_users++ == 0) {
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void release_jolt() {
    const std::lock_guard lock(g_jolt_mutex);
    if (--g_jolt_users == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

// ---- Collision layers -----------------------------------------------------------
// Static bodies never collide with each other; everything else does.
namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::BroadPhaseLayer kBroadStatic(0);
constexpr JPH::BroadPhaseLayer kBroadMoving(1);
}  // namespace layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == layers::kStatic ? layers::kBroadStatic : layers::kBroadMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == layers::kBroadStatic ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        return object == layers::kMoving || broad == layers::kBroadMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == layers::kMoving || b == layers::kMoving;
    }
};

// ---- Conversions ------------------------------------------------------------------
JPH::Vec3 to_jolt(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::Quat to_jolt(Quat q) { return {q.x, q.y, q.z, q.w}; }
Vec3 from_jolt(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Quat from_jolt(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

world::Entity entity_from_bits(std::uint64_t bits) {
    return world::Entity{static_cast<std::uint32_t>(bits & 0xFFFF'FFFFu), static_cast<std::uint32_t>(bits >> 32)};
}

bool close_to(Vec3 a, Vec3 b, float eps) { return math::length(a - b) <= eps; }
bool same_rotation(Quat a, Quat b) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::abs(d) > 1.0f - 1e-6f;
}

constexpr float kMinSize = 0.001f;

JPH::EMotionType motion_type(BodyType t) {
    switch (t) {
        case BodyType::Dynamic: return JPH::EMotionType::Dynamic;
        case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
        default: return JPH::EMotionType::Static;
    }
}

JPH::RefConst<JPH::Shape> make_shape(const Collider& c, Vec3 scale) {
    const Vec3 s{std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)};
    JPH::RefConst<JPH::Shape> shape;
    switch (c.shape) {
        case ShapeType::Sphere:
            shape = new JPH::SphereShape(std::max(kMinSize, c.radius * std::max({s.x, s.y, s.z})));
            break;
        case ShapeType::Capsule: {
            const float radius = std::max(kMinSize, c.radius * std::max(s.x, s.z));
            const float half_height = std::max(kMinSize, c.half_height * s.y);
            shape = new JPH::CapsuleShape(half_height, radius);
            break;
        }
        default: {
            const Vec3 half{std::max(kMinSize, c.half_extents.x * s.x), std::max(kMinSize, c.half_extents.y * s.y),
                            std::max(kMinSize, c.half_extents.z * s.z)};
            // Rounded edges (the convex radius) must fit inside the box.
            const float convex = std::min(JPH::cDefaultConvexRadius, std::min({half.x, half.y, half.z}) * 0.5f);
            shape = new JPH::BoxShape(to_jolt(half), convex);
            break;
        }
    }
    const Vec3 offset = c.offset * scale;
    if (math::length(offset) > 0.0f)
        shape = new JPH::RotatedTranslatedShape(to_jolt(offset), JPH::Quat::sIdentity(), shape);
    return shape;
}

// What a simulated body was built from, to notice changes.
struct Record {
    JPH::BodyID id;
    Collider collider;
    Vec3 scale;
    BodyType type = BodyType::Static;
    float mass = 0, friction = 0, restitution = 0, linear_damping = 0, angular_damping = 0, gravity_scale = 0;
    Vec3 position;  // pose and velocity as last exchanged with the ECS
    Quat rotation;
    Vec3 linear_velocity, angular_velocity;
    std::uint64_t seen = 0;
};

}  // namespace

struct PhysicsWorld::Impl {
    PhysicsDesc desc;
    BroadPhaseLayers broad_phase_layers;
    ObjectVsBroadPhase object_vs_broad_phase;
    ObjectPairs object_pairs;
    JPH::TempAllocatorMalloc temp_allocator;
    // Single-threaded: deterministic and simple. Scenes that need it can
    // move to a thread pool behind this same interface.
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;
    std::unordered_map<std::uint64_t, Record> records;
    std::uint64_t step_index = 0;

    void remove(Record& r) {
        JPH::BodyInterface& bodies = system.GetBodyInterface();
        bodies.RemoveBody(r.id);
        bodies.DestroyBody(r.id);
    }
};

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    if (impl_) {
        for (auto& [bits, record] : impl_->records) impl_->remove(record);
        impl_.reset();
        release_jolt();
    }
}

Result<std::unique_ptr<PhysicsWorld>> PhysicsWorld::create(const PhysicsDesc& desc) {
    if (desc.max_bodies == 0 || desc.max_bodies > 1'000'000)
        return Error{ErrorCode::InvalidArgument, "max_bodies must be between 1 and 1,000,000"};
    acquire_jolt();
    std::unique_ptr<PhysicsWorld> world(new PhysicsWorld());
    world->impl_ = std::make_unique<Impl>();
    Impl& impl = *world->impl_;
    impl.desc = desc;
    const JPH::uint max_bodies = desc.max_bodies;
    impl.system.Init(max_bodies, 0, std::min<JPH::uint>(max_bodies * 4, 262144), std::min<JPH::uint>(max_bodies * 2, 131072),
                     impl.broad_phase_layers, impl.object_vs_broad_phase, impl.object_pairs);
    impl.system.SetGravity(to_jolt(desc.gravity));
    return world;
}

void PhysicsWorld::set_gravity(Vec3 gravity) { impl_->system.SetGravity(to_jolt(gravity)); }
Vec3 PhysicsWorld::gravity() const { return from_jolt(impl_->system.GetGravity()); }
std::uint32_t PhysicsWorld::body_count() const { return impl_->system.GetNumBodies(); }

StepStats PhysicsWorld::step(world::World& w, float dt) {
    Impl& impl = *impl_;
    StepStats stats;
    JPH::BodyInterface& bodies = impl.system.GetBodyInterface();
    world::propagate_transforms(w);
    ++impl.step_index;
    const bool advance = dt > 0.0f && std::isfinite(dt);

    // 1) Mirror the ECS into the simulation.
    w.each<const world::GlobalTransform, const Collider>(
        [&](world::Entity e, const world::GlobalTransform& g, const Collider& collider) {
            RigidBody* rb = w.get<RigidBody>(e);
            const RigidBody settings = rb ? *rb : RigidBody{.type = BodyType::Static};
            Vec3 position, scale;
            Quat rotation;
            math::decompose(g.matrix, position, rotation, scale);

            auto it = impl.records.find(e.bits());
            const bool rebuild =
                it != impl.records.end() &&
                (!(it->second.collider == collider) || !close_to(it->second.scale, scale, 1e-4f) ||
                 it->second.type != settings.type || it->second.mass != settings.mass ||
                 it->second.linear_damping != settings.linear_damping ||
                 it->second.angular_damping != settings.angular_damping);
            if (rebuild) {
                impl.remove(it->second);
                impl.records.erase(it);
                it = impl.records.end();
            }
            if (it == impl.records.end()) {
                const bool is_static = settings.type == BodyType::Static;
                JPH::BodyCreationSettings create(make_shape(collider, scale), to_jolt(position), to_jolt(rotation),
                                                 motion_type(settings.type), is_static ? layers::kStatic : layers::kMoving);
                create.mUserData = e.bits();
                create.mFriction = std::max(0.0f, settings.friction);
                create.mRestitution = std::clamp(settings.restitution, 0.0f, 1.0f);
                create.mLinearDamping = std::max(0.0f, settings.linear_damping);
                create.mAngularDamping = std::max(0.0f, settings.angular_damping);
                create.mGravityFactor = settings.gravity_scale;
                create.mLinearVelocity = to_jolt(settings.linear_velocity);
                create.mAngularVelocity = to_jolt(settings.angular_velocity);
                if (settings.type == BodyType::Dynamic) {
                    create.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    create.mMassPropertiesOverride.mMass = std::max(1e-3f, settings.mass);
                }
                const JPH::BodyID id = bodies.CreateAndAddBody(
                    create, is_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
                if (id.IsInvalid()) {
                    if (impl.desc.logger)
                        AETHER_LOG(*impl.desc.logger, LogLevel::Warn, "physics", "body limit reached ({}); a collider was skipped",
                                   impl.desc.max_bodies);
                    return;
                }
                Record r;
                r.id = id;
                r.collider = collider;
                r.scale = scale;
                r.type = settings.type;
                r.mass = settings.mass;
                r.friction = settings.friction;
                r.restitution = settings.restitution;
                r.linear_damping = settings.linear_damping;
                r.angular_damping = settings.angular_damping;
                r.gravity_scale = settings.gravity_scale;
                r.position = position;
                r.rotation = rotation;
                r.linear_velocity = settings.linear_velocity;
                r.angular_velocity = settings.angular_velocity;
                r.seen = impl.step_index;
                impl.records.emplace(e.bits(), r);
                ++stats.created;
                return;
            }

            Record& r = it->second;
            r.seen = impl.step_index;
            if (r.friction != settings.friction) bodies.SetFriction(r.id, std::max(0.0f, r.friction = settings.friction));
            if (r.restitution != settings.restitution)
                bodies.SetRestitution(r.id, std::clamp(r.restitution = settings.restitution, 0.0f, 1.0f));
            if (r.gravity_scale != settings.gravity_scale) bodies.SetGravityFactor(r.id, r.gravity_scale = settings.gravity_scale);

            // The transform changed outside the simulation (editor, gameplay code).
            const bool moved = !close_to(r.position, position, 1e-4f) || !same_rotation(r.rotation, rotation);
            if (moved) {
                if (r.type == BodyType::Kinematic && advance) {
                    bodies.MoveKinematic(r.id, to_jolt(position), to_jolt(rotation), dt);  // sweeps, pushing bodies in the way
                } else {
                    bodies.SetPositionAndRotation(r.id, to_jolt(position), to_jolt(rotation),
                                                  r.type == BodyType::Static ? JPH::EActivation::DontActivate
                                                                             : JPH::EActivation::Activate);
                }
                r.position = position;
                r.rotation = rotation;
            } else if (r.type == BodyType::Kinematic) {
                bodies.SetLinearAndAngularVelocity(r.id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            }
            if (r.type == BodyType::Dynamic &&
                (!(r.linear_velocity == settings.linear_velocity) || !(r.angular_velocity == settings.angular_velocity))) {
                bodies.SetLinearAndAngularVelocity(r.id, to_jolt(settings.linear_velocity), to_jolt(settings.angular_velocity));
                bodies.ActivateBody(r.id);
                r.linear_velocity = settings.linear_velocity;
                r.angular_velocity = settings.angular_velocity;
            }
        });

    // Bodies whose entity or Collider is gone.
    for (auto it = impl.records.begin(); it != impl.records.end();) {
        if (it->second.seen != impl.step_index) {
            impl.remove(it->second);
            it = impl.records.erase(it);
            ++stats.removed;
        } else {
            ++it;
        }
    }

    // 2) Simulate.
    if (advance) impl.system.Update(dt, 1, &impl.temp_allocator, &impl.jobs);

    // 3) Write dynamic bodies back to their local Transform.
    for (auto& [bits, r] : impl.records) {
        if (r.type != BodyType::Dynamic) continue;
        const world::Entity e = entity_from_bits(bits);
        JPH::RVec3 jp;
        JPH::Quat jq;
        bodies.GetPositionAndRotation(r.id, jp, jq);
        JPH::Vec3 lv, av;
        bodies.GetLinearAndAngularVelocity(r.id, lv, av);
        const Vec3 position = from_jolt(JPH::Vec3(jp));
        const Quat rotation = from_jolt(jq);
        r.linear_velocity = from_jolt(lv);
        r.angular_velocity = from_jolt(av);
        if (RigidBody* rb = w.get<RigidBody>(e)) {
            rb->linear_velocity = r.linear_velocity;
            rb->angular_velocity = r.angular_velocity;
        }
        if (!bodies.IsActive(r.id)) continue;  // asleep: nothing moved
        r.position = position;
        r.rotation = rotation;
        world::Transform* t = w.get<world::Transform>(e);
        if (!t) continue;
        const world::Entity parent = world::hierarchy::parent_of(w, e);
        const world::GlobalTransform* pg = parent.is_null() ? nullptr : w.get<world::GlobalTransform>(parent);
        math::Mat4 inv;
        if (pg && math::inverse(pg->matrix, inv)) {
            // World pose -> the parent's space. The entity's own scale is kept.
            const math::Mat4 local = inv * math::Mat4::from_trs(position, rotation, r.scale);
            Vec3 unused_scale;
            math::decompose(local, t->position, t->rotation, unused_scale);
        } else {
            t->position = position;
            t->rotation = rotation;
        }
    }
    world::propagate_transforms(w);

    // Poses as the ECS now holds them (after the round trip through local
    // transforms), so the next step does not mistake rounding for an edit.
    for (auto& [bits, r] : impl.records) {
        if (r.type != BodyType::Dynamic) continue;
        if (const auto* g = w.get<world::GlobalTransform>(entity_from_bits(bits))) {
            Vec3 scale;
            math::decompose(g->matrix, r.position, r.rotation, scale);
        }
    }

    stats.bodies = impl.system.GetNumBodies();
    stats.active_bodies = impl.system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    return stats;
}

std::optional<RayHit> PhysicsWorld::raycast(Vec3 origin, Vec3 direction, float max_distance) const {
    const float len = math::length(direction);
    if (!(len > 0.0f) || !(max_distance > 0.0f)) return std::nullopt;
    const Vec3 dir = direction * (1.0f / len);
    const JPH::RRayCast ray{JPH::RVec3(to_jolt(origin)), to_jolt(dir * max_distance)};
    JPH::RayCastResult result;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, result)) return std::nullopt;
    RayHit hit;
    hit.distance = result.mFraction * max_distance;
    hit.point = origin + dir * hit.distance;
    const JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded()) return std::nullopt;
    const JPH::Body& body = lock.GetBody();
    hit.entity = entity_from_bits(body.GetUserData());
    hit.normal = from_jolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    return hit;
}

bool PhysicsWorld::add_impulse(world::Entity entity, Vec3 impulse) {
    const auto it = impl_->records.find(entity.bits());
    if (it == impl_->records.end() || it->second.type != BodyType::Dynamic) return false;
    JPH::BodyInterface& bodies = impl_->system.GetBodyInterface();
    // The record keeps the velocity last exchanged with the ECS, so the next
    // step sees the component unchanged and leaves this impulse in effect.
    bodies.AddImpulse(it->second.id, to_jolt(impulse));
    return true;
}

}  // namespace aether::simulation
