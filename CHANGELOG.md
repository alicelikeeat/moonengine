# Changelog

All notable changes are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/); versions follow [SemVer](https://semver.org/) once 1.0 ships.

## [Unreleased]

### Added
- Render module: backend-neutral `Renderer` with a Vulkan 1.3 backend (dynamic rendering, synchronization2, swapchain with resize/minimise handling, depth buffer, validation-layer support) and a headless mode with pixel readback. Camera/projection math and cube mesh. 8 tests, including GPU tests that check rendered pixels. ADR 0009.
- `samples/spinning_cubes`: windowed demo combining ECS, systems, scene hierarchy and rendering. CI publishes a Windows build as an artifact and smoke-tests the Linux build under a virtual display.
- Vendored dependencies: Vulkan-Headers 1.4.304, volk 1.4.304, GLFW 3.4 (samples only), pinned in `third_party/dependencies.json`.
- `tools/compile_shaders.py` with a CI freshness check; test harness `AETHER_SKIP` with `AETHER_TEST_NO_SKIP=1` to forbid skips in CI.
- World: scene hierarchy (`Parent`/`Children` with consistent two-way links, cycle rejection, `destroy_recursive`, `validate`), `Transform`/`GlobalTransform` and parallel `propagate_transforms`. 9 tests including a randomized forest checked against brute-force matrices and a 100,000-deep chain. ADR 0008.
- World: `get<T>(entity, ComponentId)` fast path for hot loops.
- Foundation: math library (Vec3, Quat, Mat4) with glTF-compatible conventions. 5 tests.
- World module: archetype ECS with generational handles, UUID entity identity, named components, `each`/`par_each`/`count` queries, `CommandBuffer`, and a `Schedule` that runs systems with non-conflicting declared access in parallel. 21 tests including a randomized differential test against a reference model. ADR 0007.
- Foundation: `Uuid` (v4 generation, parsing, formatting). 4 tests.
- World micro-benchmarks.
- Project governance: Apache-2.0 licence, DCO contribution process, code of conduct, security policy, governance model, roadmap.
- CMake build with module layering checks; presets for Linux (GCC/Clang/ASan/TSan) and Windows (MSVC).
- CI on GCC, Clang, MSVC with sanitizers, ACP schema validation and DCO checks.
- Foundation module: `Result`/`Error`, assertions, platform layer, heap/arena/frame/pool allocators, work-stealing job system, structured logging, Chrome-trace tracing, fixed timestep, lifecycle state machine. 32 unit tests.
- Foundation micro-benchmarks.
- Aether Control Protocol draft 0.1 (message, tool descriptor and changeset schemas, examples, validator).
- Architecture overview, ADRs 0001–0006, RFC template, build guide.
