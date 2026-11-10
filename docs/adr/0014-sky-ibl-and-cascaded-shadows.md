# 0014. Analytic sky, CPU-baked sky lighting and cascaded sun shadows

- **Status:** Accepted
- **Date:** 2026-11-10

## Context
v0.5 lit scenes with a two-colour hemisphere and showed a flat clear colour behind them. Metals had nothing to reflect, environment settings were not saved with levels, and the single shadow map covered only about 40 m around the camera target with one resolution for near and far.

## Decision
- **`Environment` is a component** (sky, horizon and ground colours, brightness, exposure, shadows, sky visibility). The renderer uses the first one in the world and falls back to `RenderOptions::environment`. Levels therefore save their sky, the editor edits it with undo, and agents see it like any other actor.
- **Analytic sky:** a gradient from zenith to horizon to ground, drawn as a full-screen pass at the far plane together with the sun's disc and glow. The same function exists in C++ (`sky_radiance`) and GLSL (`sky_gradient`).
- **Sky lighting baked on the CPU** whenever the Environment changes (`bake_sky`): diffuse irradiance as 9 spherical-harmonic coefficients in the frame uniforms, and a 32² RGBA16F cube map prefiltered with the GGX lobe per mip (mip = roughness) for reflections. The bake takes about a millisecond, needs no extra render passes or shaders, and is uploaded inside the frame's command buffer from a per-frame staging buffer. The sun is excluded from the bake because it is already a light.
- **Cascaded shadow maps:** 3 cascades of 2048² in one depth array over 80 m, split with the practical scheme (λ = 0.7). Each cascade is fitted to the bounding sphere of its view-frustum slice and snapped to texels, so shadows stay steady as the camera turns and moves; casters are culled per cascade by bounding sphere; the last cascade fades out.

## Consequences
- Reflections show the sky, not the scene: no screen-space or probe reflections yet.
- The sky is a gradient, not an image or a physical atmosphere. Image-based skies (HDR files) can reuse the same bake path with a different radiance function.
- Changing the Environment every frame (dragging a slider) re-bakes every frame, which is fine at this size.
- Three shadow passes cost about three times the single map; culling keeps small scenes cheap.
- Cascade selection is a hard switch by view depth (no blending between cascades yet).

## Alternatives considered
- **GPU prefiltering (render to cube faces):** needed for image skies at high resolution, but more passes and shaders than an analytic sky needs today.
- **Environment in the level file header or in editor settings:** not undoable or visible through the same component paths as everything else.
- **One larger shadow map / fit to the whole frustum:** wastes resolution near the camera and shimmers when the frustum changes.
