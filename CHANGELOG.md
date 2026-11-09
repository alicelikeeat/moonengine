# Changelog

All notable changes are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/); versions follow [SemVer](https://semver.org/) once 1.0 ships.

## [Unreleased]

### Added
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
