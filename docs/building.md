# Building Aether Engine

Everything builds offline from source. No account, licence server or AI service is needed.

## Requirements

- CMake 3.24+ and Ninja (or Visual Studio 2022)
- One of: MSVC 19.38+ (Visual Studio 2022 17.8+), GCC 13+, Clang 17+
- Python 3.9+ with `jsonschema` (only to validate protocol files)
- Linux only, for the samples: X11 development headers (`libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` on Ubuntu)
- Optional: `glslangValidator` (only when editing shaders)

All engine dependencies are vendored in `third_party/`; no network access or Vulkan SDK is needed to build.

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
| `AETHER_BUILD_SAMPLES` | `ON` | Build sample applications (needs a windowing system) |

## Running tests and benchmarks

```bash
./build/linux-gcc/engine/foundation/aether_foundation_tests          # all tests
./build/linux-gcc/engine/foundation/aether_foundation_tests job      # name filter
./build/linux-gcc/benchmarks/aether_foundation_bench                 # JSON lines
python3 tools/validate_protocols.py
python3 tools/compile_shaders.py --check                             # shaders up to date
```

## Running the editor

```bash
./build/linux-gcc/editor/aether_editor
```

Controls are listed in [docs/user/quick-start.md](user/quick-start.md).

## Running the sample

```bash
./build/linux-gcc/samples/spinning_cubes/aether_spinning_cubes          # Linux
build\windows-msvc\samples\spinning_cubes\Release\aether_spinning_cubes.exe   # Windows
```

Needs a Vulkan 1.3 driver. Every CI run on Windows also uploads a ready-to-run build as the `aether-spinning-cubes-windows-x64` artifact.

## GPU tests without a GPU

Install Mesa's software Vulkan driver (`mesa-vulkan-drivers` on Ubuntu) and, optionally, `vulkan-validationlayers`. The render tests then run offscreen on llvmpipe. Without any Vulkan device they are reported as skipped; `AETHER_TEST_NO_SKIP=1` turns skips into failures.

When you publish benchmark numbers, include CPU, GPU, RAM, OS, compiler, build type and commit hash.
