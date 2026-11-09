# Building Aether Engine

Everything builds offline from source. No account, licence server or AI service is needed.

## Requirements

- CMake 3.24+ and Ninja (or Visual Studio 2022)
- One of: MSVC 19.38+ (Visual Studio 2022 17.8+), GCC 13+, Clang 17+
- Python 3.9+ with `jsonschema` (only to validate protocol files)

## Windows (Visual Studio 2022)

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

## Linux

```bash
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

## Sanitizers

```bash
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan
cmake --preset linux-tsan  && cmake --build --preset linux-tsan  && ctest --preset linux-tsan
```

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `AETHER_BUILD_TESTS` | `ON` | Build unit tests |
| `AETHER_BUILD_BENCHMARKS` | `ON` | Build benchmarks in `benchmarks/` |
| `AETHER_WARNINGS_AS_ERRORS` | `OFF` (`ON` in CI) | Fail on compiler warnings |
| `AETHER_SANITIZER` | empty | `address`, `thread` or `undefined` |

## Running tests and benchmarks

```bash
./build/linux-gcc/engine/foundation/aether_foundation_tests          # all tests
./build/linux-gcc/engine/foundation/aether_foundation_tests job      # name filter
./build/linux-gcc/benchmarks/aether_foundation_bench                 # JSON lines
python3 tools/validate_protocols.py
```

When you publish benchmark numbers, include CPU, GPU, RAM, OS, compiler, build type and commit hash.
