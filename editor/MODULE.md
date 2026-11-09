# Module: Editor

- **Layer:** 5 (depends on Foundation, World, Render; nothing depends on it)
- **Owner role:** Editor maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/editor/`
- **Executable:** `app/main.cpp` → `aether_editor`
- **Tests:** `tests/` → `aether_editor_tests` (camera, picking, snapshots, undo)
- **Design:** [ADR 0010](../docs/adr/0010-editor-ui-dear-imgui.md); user guide: [docs/user/quick-start.md](../docs/user/quick-start.md)

## Structure

| File | Purpose |
| --- | --- |
| `editor_camera.*` | Orbit / look / pan / dolly / fly / frame camera |
| `picking.*` | Screen ray, ray–AABB, nearest-entity pick through the hierarchy |
| `snapshot.*` | Capture/restore entity state by Uuid (components, parent, child order) |
| `undo.*` | Undo stack and command factories (edit, create, delete, reparent) |
| `demo_scene.*` | The demo level |
| `editor_app.*` | All panels: viewport, outliner, details, place actors, log, stats, menus, toolbar, status bar |
| `theme.cpp` | Dark theme and embedded font |

## Contracts

- Commands and selection refer to entities by Uuid, never by `Entity` handle.
- Editor-known components (inspected, snapshotted): `Name`, `Transform`, `MeshInstance`, `Spin`. Others are listed read-only in Details.
- Edits made during Play are discarded by Stop and are not recorded for undo.
- The host must destroy the renderer before `ImGui::DestroyContext()`.

## Not yet implemented

Saving/loading levels (needs the scene format), multi-selection, copy/paste between levels, a content browser, reflection-driven inspectors, editor camera bookmarks, and AI assistant panels over ACP.
