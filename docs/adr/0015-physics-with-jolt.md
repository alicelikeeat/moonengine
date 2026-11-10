# 0015. Rigid body physics with Jolt Physics behind the Simulation module

- **Status:** Accepted
- **Date:** 2026-11-10

## Context
Levels need objects that fall, collide, stack and can be pushed, in the editor's Play mode and in the runtime, with data that levels can store and agents can edit. Writing a robust rigid body solver (stable stacking, continuous collision, sleeping, broad phase) is years of work that mature open-source engines have already done.

## Decision
- **Jolt Physics 5.3.0** (MIT) is the backend. It is actively maintained, used in shipped games, has no dependencies, builds with CMake on every target platform and has a permissive licence. It is vendored under `third_party/jolt` (the `Jolt/` source folder) and built with a fixed configuration: SSE4.2 on x86-64, no asserts, profiler, debug renderer or object stream.
- **New Simulation module** (layer 2, depends on Foundation and World). Its public header exposes no Jolt types, so the backend can change and other modules never include Jolt.
- **Data model:** two components. `Collider` is the shape (box, sphere, capsule) in the entity's local space, scaled by its world scale, with an optional offset. `RigidBody` is the motion (static, dynamic, kinematic; mass, friction, restitution, damping, gravity scale, velocities). A Collider without a RigidBody is static. Both are saved in levels.
- **`PhysicsWorld::step`** mirrors the ECS into the simulation every fixed step, then writes results back:
  1. Bodies are created for new entities, rebuilt when the shape, scale, type, mass or damping changed, and removed when the entity or its Collider is gone.
  2. Transforms and velocities changed by other code (editor gizmos, gameplay) are pushed into the simulation; kinematic bodies sweep to their new pose and push dynamic bodies.
  3. After the step, dynamic bodies' poses are written to their local `Transform` (through the parent's inverse matrix) and their velocities to `RigidBody`.
- **Single-threaded simulation** for now (Jolt's `JobSystemSingleThreaded`): results are bit-identical between runs on the same machine, which tests check, and there are no data races to reason about. A thread pool can replace it behind the same interface.
- **The editor simulates only while playing.** Play creates a `PhysicsWorld`; Stop destroys it and restores the level from the Play snapshot.

## Consequences
- Jolt keeps its type registry in process-wide globals; the module creates them with the first `PhysicsWorld` and destroys them with the last (an exception to ADR 0003, contained in one file).
- About 5 MB of vendored source and roughly 15 s of extra build time per configuration.
- The mirror step visits every collider each step (a hash lookup and a few comparisons per body). That is fine for thousands of bodies; larger worlds will need change tracking in the ECS.
- Not yet exposed: mesh and convex-hull colliders, constraints (joints), character controllers, triggers and contact events, collision layers, continuous collision detection settings, multithreaded stepping.
- Determinism across different CPUs or compilers is not promised (Jolt's cross-platform deterministic mode is off).

## Alternatives considered
- **Own solver:** full control, but a long road to the stability Jolt already has.
- **Bullet:** mature and permissive (zlib), but older architecture and less active development.
- **PhysX:** capable and BSD-licensed since 5.x, but much larger, with a heavier build and GPU features tied to one vendor.
- **Exposing Jolt types to the engine:** less glue, but every module would depend on one backend's API.
