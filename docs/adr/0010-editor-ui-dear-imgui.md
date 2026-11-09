# 0010. Editor UI built on Dear ImGui with a viewport texture

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The project needs a level editor whose layout and workflow are comparable to established engine editors (dockable panels, outliner, details, viewport with gizmos, undo, play-in-editor). The editor must stay fully open source, build offline, and keep Vulkan out of public engine headers.

## Decision
- **UI toolkit:** Dear ImGui (docking branch, MIT), vendored and pinned. ImGuizmo (MIT) provides transform gizmos. Roboto (Apache-2.0) is embedded as the UI font.
- **Rendering:** `RendererDesc::enable_ui` makes the renderer draw the scene into an offscreen viewport texture and then draw the ImGui frame to the window. The viewport image is rendered through an sRGB view and sampled through a UNORM view, with a UNORM swapchain, so ImGui colours and scene colours are both correct. Viewport resizes are applied after the frame that still references the old texture.
- **Editor module (`editor/`, layer 5):** UI-free core (viewport camera, picking, snapshots, undo) with unit tests, plus `EditorApp` (all panels) whose public header has no ImGui types.
- **Undo and Play mode share one mechanism:** entity snapshots keyed by Uuid (components, parent link, child order). Commands never store runtime Entity handles. Play captures the level; Stop restores it.
- **Ground grid:** an analytic, anti-aliased grid drawn by a fragment shader on a large quad, fading with distance (line geometry clipped poorly at the near plane on some drivers).

## Consequences
- Fast iteration on editor features; immediate-mode UI is easy to test visually and to extend with tool panels (including future AI panels).
- ImGui's look differs from native UI toolkits; the editor applies its own dark theme.
- Component inspection is hand-written per component for now. A reflection system will replace it so every component, including plug-in components, gets an inspector automatically.
- `imgui_internal.h` (DockBuilder, side bars) is used; ImGui upgrades need checking.

## Alternatives considered
- **Qt:** heavier, LGPL, large binary and build-time cost.
- **Custom retained-mode UI:** large effort before any editor features exist.
- **Web-based editor UI:** adds a browser runtime and an IPC layer; possible later for remote/AI tooling via ACP.
