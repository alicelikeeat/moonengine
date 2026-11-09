# Aether Engine

**An open source, AI-native 3D game engine.** Apache-2.0. No royalties, no seat fees, no account, no online check. Builds and runs fully offline.

> 完全开源 · AI 原生 · 下一代 3D 游戏引擎。Apache-2.0 许可，无版税、无席位费、无需账号或联网验证，可完全离线构建与运行。

## Status: early development (milestone M0, start of M1)

This is a young project. Here is exactly what exists today and what does not.

| Area | State |
| --- | --- |
| Governance, licence, contribution process (DCO) | ✅ Done |
| CMake build, presets, CI (GCC, Clang, MSVC, ASan/UBSan, TSan) | ✅ Done |
| Foundation: allocators, work-stealing jobs, logging, tracing, time, lifecycle, UUIDs, math | ✅ Implemented and tested (41 tests) |
| World: archetype ECS, queries, command buffers, parallel system schedule, scene hierarchy, transforms | ✅ Implemented and tested (30 tests) |
| Aether Control Protocol (ACP) for tools and AI agents | 📝 Draft 0.1 schemas + validator |
| Render: Vulkan 1.3 renderer, windowed and headless | ✅ First slice, tested on software Vulkan (8 tests) |
| Sample app: `samples/spinning_cubes` (Windows `.exe` from CI) | ✅ Runs; needs a Vulkan 1.3 GPU driver |
| Scene save format, materials, Assets, Editor, Physics, Animation, AI agents | ⏳ Not started — see [ROADMAP.md](ROADMAP.md) |

There is a first renderer and a demo program, but no editor yet. Claims about performance versus other engines will only be made with published, reproducible benchmarks.

## Principles

1. Everything is open: runtime, renderer, editor, AI agent framework, build and test tooling.
2. Your games and assets are yours. The engine claims nothing.
3. AI is optional. Every feature works without an AI model; no proprietary AI API is ever required. Cloud models can be plugged in as optional providers, and local open-weight models are first-class.
4. AI agents use the same public protocol ([ACP](protocols/acp/README.md)) as any script, with permissions, transactions, rollback and an audit log.
5. Results over claims: nothing is "done" until it builds, passes tests and can be reproduced by someone else.

## Quick start

```bash
# Linux
cmake --preset linux-gcc && cmake --build --preset linux-gcc && ctest --preset linux-gcc

# Windows (Visual Studio 2022)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Full instructions: [docs/building.md](docs/building.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `engine/foundation` | Core runtime: memory, jobs, logging, tracing, time, lifecycle, UUIDs |
| `engine/world` | Entity component system: entities, components, queries, systems |
| `engine/render` | Renderer interface and Vulkan backend |
| `samples` | Runnable demo programs |
| `protocols/acp` | Aether Control Protocol schemas, examples and spec |
| `tests/support` | Dependency-free unit test harness |
| `benchmarks` | Micro-benchmarks (JSON output) |
| `docs` | Architecture, ADRs, RFCs, build guide |
| `tools` | Protocol validator, DCO checker |
| `third_party` | Dependency manifest and licence records |

Directories for Editor, Intelligence and the other systems are added when they have working code and tests, not before. The planned layout is in [docs/architecture/overview.md](docs/architecture/overview.md).

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). Sign off your commits (`git commit -s`); you keep your copyright. Big changes start as an [RFC](docs/rfc/0000-template.md).

- Governance: [GOVERNANCE.md](GOVERNANCE.md)
- Code of conduct: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- Security: [SECURITY.md](SECURITY.md)
- Help: [SUPPORT.md](SUPPORT.md)

## License

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
