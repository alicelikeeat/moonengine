# Module: Assets

- **Layer:** 1 (depends on Foundation)
- **Owner role:** Assets maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/assets/`
- **Third-party:** stb_image 2.30 (PNG, JPEG), linked privately
- **Tests:** `tests/` → `aether_assets_tests` (including a 3,000-case GLB fuzz test)
- **Design:** [ADR 0012](../../docs/adr/0012-gltf-import-and-model-library.md)

Importers that turn files into renderer-independent data. No GPU code.

| Type / function | Purpose |
| --- | --- |
| `load_gltf_file(path)` | Imports `.gltf` (with external or embedded buffers) and `.glb` |
| `parse_gltf`, `parse_glb` | The same from memory; `base_dir` resolves relative URIs (empty: none allowed) |
| `Model`, `Mesh`, `Primitive`, `Material`, `Node`, `TextureImage` | Imported data: triangle lists with tangents, metallic-roughness materials (factors, base colour, metallic-roughness and normal textures, emission), node TRS hierarchy |
| `generate_tangents` | Tangents from UVs when a file has none (bitangent points up the image, as glTF expects) |
| `ImportOptions` | Limits: file size, total vertices, texture size |
| `decode_image`, `decode_base64` | PNG/JPEG to RGBA8; base64 for data URIs |

## Contracts

- Files are untrusted. Every error is a `Result` error with the file name; nothing is partially returned.
- External URIs must be relative and stay inside the model's folder (no schemes, absolute paths or `..`).
- Primitives are triangle lists with normals (computed when missing) and UVs (zero when missing). Non-triangle primitives are kept as empty entries so indices stay stable, with a warning.
- Unsupported features are listed in `Model::warnings`; required extensions are refused.

## Not yet implemented

Skins, morph targets and animations; occlusion and emissive maps; normal map scale in the renderer; KTX2/Basis textures; Draco and meshopt compression; sparse accessors; multiple scenes (only the default scene is imported); an asset database with content hashes and import caching.
