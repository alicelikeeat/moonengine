# Third-party dependencies

Foundation and World use only the C++ standard library and OS APIs (ADR 0006). Third-party code is **vendored** (copied into this directory, trimmed, and pinned) so the engine builds fully offline. Full licence texts are in [`licenses/`](licenses/).

| Dependency | Version | Licence | Used by | Why |
| --- | --- | --- | --- | --- |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | 1.4.304 | Apache-2.0 OR MIT | render, samples | Vulkan API declarations |
| [volk](https://github.com/zeux/volk) | 1.4.304 | MIT | render | Loads Vulkan at run time; no SDK needed to build or run |
| [GLFW](https://www.glfw.org/) | 3.4 | Zlib | samples, editor app | Windows and input for applications |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.5 docking | MIT | render (UI pass), editor | Immediate-mode UI for the editor and debug overlays |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | pinned commit | MIT | editor | Translate/rotate/scale gizmos |
| [Roboto](https://fonts.google.com/specimen/Roboto) Medium | from ImGui misc/fonts | Apache-2.0 | editor | UI font, embedded in the executable |
| [stb_image](https://github.com/nothings/stb) | 2.30 | MIT OR Unlicense | assets | PNG and JPEG decoding for model textures (other decoders compiled out) |

The engine library never links GLFW; windowing stays in applications. The render module links Dear ImGui only to draw an optional UI layer.

Developer tooling (not shipped):

| Tool | Version | Licence | Used for |
| --- | --- | --- | --- |
| jsonschema (Python) | 4.23.0 | MIT | Validating ACP protocol files in CI |
| glslang (`glslangValidator`) | 15.1.0 | BSD-3-Clause and others | Compiling shaders; the SPIR-V output is committed |
| Mesa lavapipe | system package | MIT | Software Vulkan device for GPU tests in CI |

## Updating a vendored dependency

1. Download the exact release (or commit) and verify its hash.
2. Replace the directory, applying the same trimming as recorded in `dependencies.json`.
3. Update `dependencies.json` (version, commit or SHA-256) and `licenses/`.
4. Build and run all tests, including the render tests and the sample smoke test.

## Adding a dependency

1. Open an issue explaining why it is needed and what alternatives exist.
2. Licence must be Apache-2.0 compatible (MIT, BSD, Zlib, Apache-2.0, Boost, ISC, …). No GPL/AGPL in anything linked into the engine or editor. No non-redistributable or "source-available" code.
3. Pin an exact version or commit, with a SHA-256 of the source archive.
4. Add an entry to `dependencies.json` (name, version, licence, upstream URL, SHA-256, which modules use it) and copy its licence text to `third_party/licenses/<name>.txt`.
5. Module owner and one maintainer approve.

`dependencies.json` is the source for the SBOM generated at release time.
