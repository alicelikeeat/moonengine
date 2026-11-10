# Module: Simulation

- **Layer:** 2 (depends on Foundation and World)
- **Owner role:** Simulation maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/simulation/` (no Jolt types)
- **Backend:** Jolt Physics 5.3.0, linked privately (`src/physics.cpp`)
- **Tests:** `tests/` → `aether_simulation_tests`
- **Design:** [ADR 0015](../../docs/adr/0015-physics-with-jolt.md)

Rigid body physics for the editor's Play mode and the runtime.

| Type / function | Purpose |
| --- | --- |
| `Collider` | Component: box, sphere or capsule in the entity's local space, scaled with the entity, with an optional offset. Alone, it is a static obstacle. |
| `RigidBody` | Component: static, dynamic or kinematic; mass, friction, restitution, damping, gravity scale; linear and angular velocity (written back each step). |
| `PhysicsWorld` | The simulation. `step(world, dt)` mirrors the ECS, advances one fixed step and writes results back. `raycast`, `add_impulse`, `set_gravity`. |
| `register_components(SerializerRegistry&)` | Level-file support for both components. |

## Contracts

- Call `step` with a fixed `dt` (1/60 s). It runs `propagate_transforms` before and after, so `GlobalTransform` is current when it returns. `dt = 0` only synchronises bodies.
- Changing a `Transform` or a `RigidBody` velocity between steps moves the body or sets its velocity. Changing the collider, scale, body type, mass or damping rebuilds the body.
- Dynamic bodies write their pose to the local `Transform`, also under parents; they should not be children of other dynamic bodies.
- The simulation is single-threaded and repeats bit-identically on the same machine and build.
- A contact uses the larger of the two bodies' restitution and the geometric mean of their friction (Jolt's defaults).

## Not yet implemented

Mesh and convex-hull colliders, joints, character controller, triggers and contact events, collision layers and filters, multithreaded stepping, debug drawing in the renderer (the editor draws the selected collider as an overlay), interpolation between fixed steps.
