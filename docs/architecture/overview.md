# Architecture overview

Aether Engine is split into nine first-level systems. Each system is a separate library with a public interface under `include/aether/<system>/`, its own tests, and an ownership file (`MODULE.md`).

## Systems and layering

Dependencies only point **down** the layers. The rule is enforced at configure time by `aether_add_module` in `cmake/AetherModule.cmake`: a module that declares a dependency on a module in the same or a higher layer fails to configure.

| Layer | System | Directory | Responsibility | Status |
| --- | --- | --- | --- | --- |
| 0 | Foundation | `engine/foundation` | Memory, threads, jobs, logging, tracing, time, lifecycle, platform | **Implemented (core subset)** |
| 1 | World | `engine/world` | ECS, scenes, streaming, stable IDs | **Implemented (ECS core)** |
| 1 | Assets | `engine/assets` | Import, conversion, caching, dependency graph | **First slice: glTF 2.0 import** |
| 2 | Render | `engine/render` | RHI, render graph, materials, lighting | **Vulkan 1.3 renderer: PBR, lights, sun shadows** |
| 2 | Simulation | `engine/simulation` | Physics, collision, fixed step, replay | Planned (M4) |
| 2 | Animation | `engine/animation` | Skeletons, animation graph, IK | Planned (M4) |
| 3 | Gameplay | `engine/gameplay` | Scripting, events, game logic | **Started: Spin component and system** |
| 4 | Intelligence | `intelligence` | Model providers, agents, ACP tools | Planned (M3) |
| 5 | Editor | `editor` | Editing UI, debugging, profiling | **First version: level editor** |

Rules:

- **No hidden globals.** Systems receive what they need (allocator, logger, job system) through explicit construction. Process-wide state is limited to the assertion handler and the thread index counter in Foundation.
- **No reaching into internals.** Only headers under `include/` are public. `src/` is private to the module.
- **The runtime never needs the editor, an AI model or a network service to start.** Intelligence and Editor sit at the top so nothing below can depend on them.
- **Persistent identity is never a pointer.** Scenes, entities and assets use stable IDs.
- **Cross-language and cross-process interfaces** (ACP, plug-in ABI) never expose C++ object layout.

## Foundation today

| Component | Header | Notes |
| --- | --- | --- |
| Result / Error | `result.hpp` | Stable `ErrorCode` values shared with ACP |
| Assertions | `assert.hpp` | Always on; pluggable handler |
| Platform | `platform.hpp` | OS id, thread naming, thread index, monotonic clock |
| Allocators | `memory.hpp` | Heap (with stats), Arena, Frame, Pool |
| Job system | `job_system.hpp` | Work stealing, cooperative wait, `parallel_for` |
| Logging | `log.hpp` | Text, JSON Lines and in-memory sinks |
| Tracing | `trace.hpp` | Chrome Trace Event JSON (open in Perfetto) |
| Time | `time.hpp` | Stopwatch; integer-nanosecond fixed timestep |
| Lifecycle | `lifecycle.hpp` | Init / run / pause / device lost / shutdown state machine |
| UUID | `uuid.hpp` | v4 UUIDs for persistent identity of entities, scenes, assets |
| Math | `math.hpp` | Vec3, Quat, Mat4 (right-handed, Y up, column-major, glTF conventions) |

Not yet in Foundation (tracked in [ROADMAP.md](../../ROADMAP.md)): async file I/O, resource handles, crash dumps, and a lock-free work-stealing deque.

## World today

Archetype ECS ([ADR 0007](../adr/0007-archetype-ecs.md)): generational `Entity` handles, UUID identity, named components, `each`/`par_each` queries, `CommandBuffer`, a `Schedule` that runs non-conflicting systems in parallel, and a scene hierarchy with transform propagation ([ADR 0008](../adr/0008-scene-hierarchy.md)). Details in [`engine/world/MODULE.md`](../../engine/world/MODULE.md).

## Render today

A Vulkan 1.3 renderer behind a backend-neutral `Renderer` interface ([ADR 0009](../adr/0009-first-vulkan-renderer.md)): windowed or headless, draws entities with `GlobalTransform` + `MeshInstance`. The `samples/spinning_cubes` program ties ECS, systems, hierarchy and rendering together. Details in [`engine/render/MODULE.md`](../../engine/render/MODULE.md).

## Editor today

A docking level editor on Dear ImGui ([ADR 0010](../adr/0010-editor-ui-dear-imgui.md)): viewport with gizmos and picking, outliner, details, undo/redo and play-in-editor. See [`editor/MODULE.md`](../../editor/MODULE.md).

## Level files

Levels are JSON (`.alevel`, [format spec](../formats/level-v1.md), [ADR 0011](../adr/0011-json-level-format.md)). Each module registers serializers for its components; unknown data is preserved.

## Verification

Every module is built with warnings as errors on GCC, Clang and MSVC and its tests are run under AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer in CI (see `.github/workflows/ci.yml`).
