# Module: World

- **Layer:** 1 (depends on Foundation only)
- **Owner role:** World maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/world/`
- **Tests:** `tests/` → `aether_world_tests`
- **Benchmarks:** `benchmarks/world_bench.cpp`
- **Design:** [ADR 0007](../../docs/adr/0007-archetype-ecs.md)

## What it provides

| Type | Purpose |
| --- | --- |
| `Entity` | Runtime handle: index + generation. Stale handles are detected. |
| `World` | Entities, components, queries (`each`, `par_each`, `count`). |
| `Uuid` (Foundation) | Persistent entity identity: `World::id_of`, `World::find`, `World::create_with_id`. |
| `ComponentInfo` | Name, size, alignment and lifetime operations for each component type. |
| `CommandBuffer` | Deferred create/destroy/add/remove, applied in order. |
| `Schedule`, `SystemAccess`, `SystemContext` | Systems with declared read/write access, run in parallel waves. |
| `Transform`, `GlobalTransform`, `Parent`, `Children` | Scene hierarchy components ([ADR 0008](../../docs/adr/0008-scene-hierarchy.md)). |
| `hierarchy::set_parent`, `clear_parent`, `destroy_recursive`, `validate` | Keep both sides of parent/child links consistent; reject cycles. |
| `propagate_transforms` | Computes world matrices root to leaf, optionally in parallel. |

## Contracts

- Components must be nothrow move-constructible and nothrow destructible. They need not be copyable or assignable.
- Give components that will be saved or exposed to tools a `static constexpr std::string_view kComponentName`. Names must be unique per World.
- Queries may run concurrently with other queries. Structural changes (create, destroy, adding a component the entity lacks, remove) must not overlap anything; inside queries or systems use a `CommandBuffer`. Violations trigger an assertion.
- Inside a `Schedule`, a system can only query components it declared; `const T` needs read access, `T` needs write access.
- Command buffers from one `Schedule::run` are applied after the last wave, in system registration order.
- Entity handles are not persistent. Save the `Uuid`.
- Change parent/child links only through `hierarchy::` functions. An entity whose parent is dead is treated as a root.
- `GlobalTransform` is written by `propagate_transforms`; treat it as read-only elsewhere.
- Hot loops can resolve a `ComponentId` once and call `get<T>(entity, id)` to skip the type lookup.

## Thread-safety summary

| Operation | Concurrent with queries? |
| --- | --- |
| `each`, `par_each`, `count`, `get`, `has`, `alive`, `id_of`, `find` | Yes |
| Replacing an existing component with `add` | Yes, if no other thread touches that entity's component |
| `create`, `destroy`, `remove`, `add` of a new component type, first use of a component type | No |

## Not yet implemented

Serialization format v1 with migrations, world partition and streaming, floating origin, prefabs, batched entity creation, change detection and depth-sorted transform propagation. See `ROADMAP.md`.
