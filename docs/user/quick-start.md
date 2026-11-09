# Aether Engine — Quick Start

This package contains two programs. Both need a GPU driver with **Vulkan 1.3** (NVIDIA, AMD and Intel drivers from 2022 onward).

## aether_editor.exe — the level editor

Opens with a demo level (a small solar system). Panels can be dragged, docked and tabbed; *Window > Reset Layout* restores the default.

| Panel | What it does |
| --- | --- |
| Viewport | 3D view. Click to select; drag the gizmo to move, rotate or scale |
| Outliner | Scene tree. Click to select, drag onto another item to attach, drag to empty space to detach, right-click for actions, search box filters by name |
| Details | Name, Location / Rotation / Scale, mesh colour, Spin settings, add/remove components |
| Place Actors | Add a Cube, Spinning Cube or Empty actor at the view focus |
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
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+D / Delete | Duplicate / Delete |
| Alt+P / Esc | Play-Pause / Stop |
| G | Toggle grid |

Layout is saved to `aether_editor.ini` next to where the editor was started.

## aether_spinning_cubes.exe — runtime demo

The same solar system running as a game would, with no editor. Esc quits. Options: `--validation`, `--no-vsync`, `--frames N`.

## Licence

Apache-2.0. Third-party licences are in `licenses/`. Verify downloads with `SHA256SUMS.txt`.
