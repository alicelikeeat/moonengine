# 0001. C++20 and CMake

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The engine must build on Windows 11 (primary development platform), Linux and later macOS/iOS with mainstream, freely available toolchains.

## Decision
Use ISO C++20 without compiler extensions, and CMake ≥ 3.24 with presets. Supported compilers: MSVC 19.38+ (VS 2022 17.8), GCC 13+, Clang 17+.

## Consequences
- Concepts, `std::format`, designated initialisers and `std::span` are available everywhere.
- `std::expected` (C++23) is not available, so Foundation provides `aether::Result<T>`.
- Moving to C++23 later needs a new ADR.

## Alternatives considered
- **C++17:** lacks concepts and `std::format`.
- **Bazel / Meson:** smaller Windows/IDE support and contributor familiarity than CMake.
