# 0013. Metallic-roughness PBR, light components and a sun shadow map

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
Until v0.4 every surface was lit by one hard-coded light with a fixed ambient term. Imported glTF models carry metallic-roughness materials and normal maps that were ignored, levels could not contain lights, and nothing cast shadows. Making scenes look like a modern engine's needs physically based shading, lights that users and agents place and edit like any actor, and shadows.

## Decision
- **Shading model: glTF metallic-roughness**, so imported materials render as their authors intended: GGX distribution, height-correlated Smith visibility, Schlick Fresnel, Lambert diffuse; base colour, metallic, roughness and normal textures; emission. Output goes through **ACES (Narkowicz fit)** with an exposure control.
- **Ambient: hemisphere sky light** (sky and ground colours) for diffuse, and the same hemisphere sampled along the reflection with an analytic split-sum approximation for specular. It is cheap and keeps metals from turning black, until image-based lighting arrives.
- **`Light` component** (render module), serialised in levels: `directional` (travels along the entity's -Z) and `point` (smooth inverse-square falloff to zero at `range`). Lights are entities with a transform, so the editor, undo, hierarchy and ACP treat them like any actor. Up to 16 point lights per frame, nearest to the camera first. A world with no lights gets a default sun, so older levels still render.
- **Sun shadows:** one 2048² depth map for the first shadow-casting directional light, fitted each frame around the casters within 40 m of the camera target, snapped to texels to avoid shimmering, with normal-offset and slope-scaled bias and 3×3 hardware-filtered PCF.
- **Descriptors:** one set per frame in flight holding all textures as one array (1,024 slots, unused slots show white), the frame's uniform buffer (camera, lights, shadow matrix) and the shadow map. Draws select textures by index through push constants, so changing materials costs no descriptor binds. This needs `shaderSampledImageArrayDynamicIndexing` and 1,025 samplers per stage, which every Vulkan 1.3 desktop driver provides; devices without them are rejected with a clear reason.
- **Material data lives in `MeshInstance`** (colour, metallic, roughness, emission, three texture slots), written to levels only when not default, rather than in a separate material asset for now.

## Consequences
- Imported models look right without manual tweaking, and lighting is controllable per level.
- Forward shading of every light per pixel is fine for tens of lights but does not scale to hundreds; clustered lighting is the planned next step.
- One shadow map limits shadow detail in large outdoor scenes; cascaded shadow maps will replace it.
- Point lights do not cast shadows yet.
- Materials are per instance; a shared material asset with a material editor will come with the asset database.
- Environment settings (sky, exposure) are editor-session settings, not yet stored in levels.

## Alternatives considered
- **Blinn-Phong:** simpler, but cannot represent glTF materials faithfully.
- **Deferred shading:** scales to more lights, but costs bandwidth and complicates transparency and MSAA; a clustered forward renderer keeps both options open.
- **Descriptor set per material or texture:** what v0.4 did; it does not extend to three textures per draw without many sets or binds.
- **Cascaded shadow maps now:** better quality, more code and passes; one fitted map is a working first step that the same pipeline can grow into.
