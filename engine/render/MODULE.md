# Module: Render

- **Layer:** 2 (depends on Foundation, World and Assets)
- **Owner role:** Render maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/render/` (no Vulkan types)
- **Backend:** Vulkan 1.3 via volk (`src/vulkan_renderer.cpp`)
- **Shaders:** GLSL in `shaders/`, compiled to SPIR-V by `tools/compile_shaders.py` into `src/generated/shaders.inl`
- **Tests:** `tests/` → `aether_render_tests` (math tests + offscreen GPU tests with pixel checks)
- **Design:** [ADR 0009](../../docs/adr/0009-first-vulkan-renderer.md), [ADR 0012](../../docs/adr/0012-gltf-import-and-model-library.md) (textures, models), [ADR 0013](../../docs/adr/0013-pbr-lights-and-shadows.md) (PBR, lights, shadows)

## What it provides

| Type | Purpose |
| --- | --- |
| `Renderer` | Abstract renderer; `Renderer::create` returns the Vulkan backend. Windowed or headless. |
| `RendererDesc` | Size, vsync, validation, window-system extensions and surface callback, logger. |
| `Camera`, `look_at`, `perspective`, `view_projection` | Right-handed camera with Vulkan clip space (depth 0..1, Y flipped). |
| `MeshData`, `make_cube`, `compute_bounds`, `MeshInstance` | CPU meshes (position, normal, UV, tangent), bounds, and the component that draws an entity with a metallic-roughness material. |
| `Light`, `rotation_toward` | Directional and point light component; helper to aim a directional light. |
| `Environment` (in `RenderOptions`) | Sky light colours and intensity, exposure, shadows on/off. |
| `upload_texture`, `TextureData` | RGBA8 textures (sRGB or linear) with mipmaps; texture 0 is white; up to 1,024. |
| `MeshRegistry`, `register_components` | Stable mesh and texture names for level files, with an on-demand resolver. |
| `ModelLibrary` | Content folder of glTF models: import, upload, name, list, spawn as entity hierarchies, bounds for picking. |
| `RenderOptions` | Selection highlight and ground grid per frame. |
| UI mode (`RendererDesc::enable_ui`) | Scene into a viewport texture (`viewport_texture()`), ImGui frame to the window. |

## Contracts

- The renderer draws every entity with `GlobalTransform` and `MeshInstance`; run `propagate_transforms` first.
- `render()` iterates the World, so it must not overlap structural changes.
- Shading: GGX metallic-roughness PBR with normal maps and emission, the first directional light (with a 2048² shadow map) plus up to 16 point lights, hemisphere ambient, ACES tone mapping. Without any `Light` in the world a default sun is used.
- Base colour = `MeshInstance::color` × base colour texture (sRGB textures are linearised by the sampler).
- `ModelLibrary` paths are relative to its content folder and may not leave it.
- Headless frames are synchronous; `read_pixels()` returns the last frame as RGBA8, top row first.
- After editing a shader, run `python3 tools/compile_shaders.py`; CI fails if the embedded SPIR-V is stale.
- Requires Vulkan 1.3 with `dynamicRendering` and `synchronization2`. Missing support returns an `unsupported` error with the reason per device.

## Testing without a GPU

The GPU tests render offscreen and check pixels. They run on any Vulkan 1.3 device, including Mesa's software rasteriser (lavapipe), which CI installs. Without any device they report `[SKIP]`; set `AETHER_TEST_NO_SKIP=1` to make skips fail. Sanitizer reports from inside the Mesa driver are suppressed for these tests only (`tools/sanitizers/`).

## Not yet implemented

Render graph, MSAA for the editor viewport, device-local memory with staging uploads and a real allocator, image-based lighting, cascaded shadows and point-light shadows, clustered lighting for many lights, shared material assets, texture streaming and sharing between files, transparency, GPU-driven culling, MSAA/TAA, Metal backend.
