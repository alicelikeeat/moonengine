# Roadmap

Milestones are complete only with running code, tests, updated architecture docs and a changelog entry. Status is updated as work lands; nothing is marked done in advance.

## M0 — Open source foundation · ✅ Done

- [x] Apache-2.0 licence, NOTICE, README, CONTRIBUTING (DCO), CODE_OF_CONDUCT, SECURITY, GOVERNANCE, SUPPORT
- [x] ADR and RFC process with first ADRs
- [x] CMake build with enforced module layering, presets for Linux and Windows
- [x] CI: GCC, Clang, MSVC; AddressSanitizer + UBSan; ThreadSanitizer; DCO check; SHA-pinned actions
- [x] In-tree test harness
- [x] ACP draft 0.1: message, tool and changeset schemas, examples, validator

## M1 — Runnable core · 🚧 In progress

Foundation
- [x] Result/Error types, assertions, platform layer
- [x] Heap, arena, frame and pool allocators
- [x] Work-stealing job system with cooperative waits
- [x] Structured logging (text, JSON Lines, memory)
- [x] Chrome-trace performance tracing
- [x] Fixed-step clock, lifecycle state machine
- [ ] Async file I/O and resource handles
- [ ] Crash handler with per-thread stack capture and minidumps
- [ ] Lock-free work-stealing deque (current deques are mutex-based; ~0.8 µs per empty job on a 4-thread VM)
- [ ] Public Foundation benchmark results on reference hardware (RTX 5070 Ti / 32 GB / Windows 11)

World
- [ ] Archetype ECS with stable generational entity IDs
- [ ] Scene hierarchy, serialization format v1 (documented), migration hooks

Render
- [ ] RHI interface; Vulkan 1.3 backend
- [ ] Render graph; forward+ clustered PBR; first triangle → lit scene sample

## M2 — Editable 3D scenes
glTF 2.0 import, content-hashed asset database, materials, scene save/load, editor shell with viewport, hierarchy, inspector, undo/redo.

## M3 — AI-native workflow
ACP server over stdio and loopback WebSocket; scene query, transactional edits, rollback; model provider interface with a no-AI mode and a local open-weights provider; agent roles (planner, implementer, reviewer, tester) with budget and audit logs.

## M4 — Complete game runtime
Physics (evaluate Jolt Physics, MIT) behind an engine interface, skeletal animation and animation graph, sandboxed scripting, a packaged sample game.

## M5 — Cross-platform
Linux parity, native Metal backend, iOS device validation, published platform differences.

## M6 — Advanced rendering research
Virtual geometry, dynamic GI, glass rendering system, GPU-driven pipelines, optional neural techniques — each as an isolated, benchmarked module.

## M7 — Stable release
SDK, plug-in system, complete docs, reproducible benchmarks against comparable engines under equal conditions, SBOM and signed releases.

## Benchmark targets (not yet measured)

These are goals, not claims:

1. At least one public, reproducible rendering performance advantage over Unreal Engine 5 in an equivalent scene at equivalent quality.
2. ≥ 80% autonomous completion on a fixed set of 100 editing tasks, reporting failures and cost.
3. 50% less manual time on a defined scene-authoring test.
4. Third parties build the engine from public docs alone.
5. Full engine and editor run with no account, commercial model or online verification.
