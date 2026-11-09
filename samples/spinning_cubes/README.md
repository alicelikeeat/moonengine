# Aether Engine — Spinning Cubes

A small solar system of cubes: the sun spins, six planets orbit it, and moons orbit the planets. Everything moves through the engine's scene hierarchy: each body only spins locally, and parent transforms carry the orbits.

## Run

Double-click `aether_spinning_cubes.exe`. Press **Esc** to quit.

Requires a GPU driver with **Vulkan 1.3** (NVIDIA, AMD and Intel drivers from 2022 onward). The window title shows the GPU in use and the frame rate.

Command-line options:

| Option | Effect |
| --- | --- |
| `--validation` | Enable Khronos validation layers (if the Vulkan SDK is installed) |
| `--no-vsync` | Present as fast as possible instead of at the display refresh rate |
| `--frames N` | Quit after N frames |

## What it exercises

- ECS entities and components (`Transform`, `Spin`, `MeshInstance`)
- A `Schedule` system updating `Spin` → `Transform`
- Scene hierarchy and parallel transform propagation
- Fixed 60 Hz simulation step
- The Vulkan 1.3 renderer (dynamic rendering, depth buffer, swapchain resize)

## Licence

Aether Engine is Apache-2.0. Third-party licences are in `licenses/`.
