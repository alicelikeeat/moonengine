# Aether Engine — Quick Start

This package contains two programs. Both need a GPU driver with **Vulkan 1.3** (NVIDIA, AMD and Intel drivers from 2022 onward).

## aether_editor.exe — the level editor

Opens with a demo level (a small solar system). Panels can be dragged, docked and tabbed; *Window > Reset Layout* restores the default.

**Levels** are saved as `.alevel` files (*File > Save*, Ctrl+S). Open them with *File > Open Level* (Ctrl+O), by dragging a file onto the editor window, or with `aether_editor.exe path\to\level.alevel`. The title bar shows `*` when there are unsaved changes, and the editor asks before discarding them. `levels/solar_system.alevel` is the demo level as a file.

| Panel | What it does |
| --- | --- |
| Viewport | 3D view. Click to select; drag the gizmo to move, rotate or scale |
| Outliner | Scene tree. Click to select, drag onto another item to attach, drag to empty space to detach, right-click for actions, search box filters by name |
| Details | Name, Location / Rotation / Scale, mesh colour, Spin settings, add/remove components |
| Place Actors | Add a Cube, Spinning Cube or Empty actor at the view focus |
| Content Browser | 3D models (`.gltf`, `.glb`) in the `Content` folder. Double-click to add one at the view focus, or drag it into the Viewport to place it on the ground |
| Output Log | Engine and editor messages, filterable |
| Stats | Frame time graph, GPU, draw calls, entity counts |

Toolbar: **Play / Pause / Stop** (simulation; Stop restores the level), **Move / Rotate / Scale**, **World / Local** gizmo space, **Snap**, **Grid**, camera speed.

### Controls

| Input | Action |
| --- | --- |
| LMB click | Select |
| RMB drag | Look around |
| RMB + W A S D, Q E | Fly (Shift = faster, wheel while flying = speed) |
| Alt + LMB drag | Orbit |
| MMB drag | Pan |
| Mouse wheel | Zoom |
| F | Focus selection (or double-click in the Outliner) |
| W / E / R | Move / Rotate / Scale gizmo |
| Ctrl while dragging | Toggle snapping |
| Ctrl+N / Ctrl+O | New / Open level |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+D / Delete | Duplicate / Delete |
| Alt+P / Esc | Play-Pause / Stop |
| G | Toggle grid |

Layout is saved to `aether_editor.ini` next to where the editor was started.

### Your own models

Copy `.gltf` or `.glb` files (with their `.bin` and texture files) anywhere under `Content\` next to the editor and press **Refresh** in the Content Browser. Supported: meshes, node hierarchy, base colour and PNG/JPEG base colour textures. Not yet: animations, skinning, normal/roughness maps (they are skipped with a note in the Output Log). Use `--content D:\MyModels` to browse another folder.

Levels store the model's path inside the content folder, so keep levels and the `Content` folder together when you share them. `levels/model_garden.alevel` uses the sample models.

## aether_spinning_cubes.exe — runtime demo

The same solar system running as a game would, with no editor. Esc quits. Run any level saved by the editor with `--level path\to\level.alevel` (models load from `Content\`, or `--content DIR`). Other options: `--validation`, `--no-vsync`, `--frames N`.

## Licence

Apache-2.0. Third-party licences are in `licenses/`. Verify downloads with `SHA256SUMS.txt`.
