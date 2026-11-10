# Aether Engine — Quick Start

This package contains two programs. Both need a GPU driver with **Vulkan 1.3** (NVIDIA, AMD and Intel drivers from 2022 onward).

## aether_editor.exe — the level editor

Opens with a demo level (a small solar system). Panels can be dragged, docked and tabbed; *Window > Reset Layout* restores the default.

**Levels** are saved as `.alevel` files (*File > Save*, Ctrl+S). Open them with *File > Open Level* (Ctrl+O), by dragging a file onto the editor window, or with `aether_editor.exe path\to\level.alevel`. The title bar shows `*` when there are unsaved changes, and the editor asks before discarding them. `levels/solar_system.alevel` is the demo level as a file.

| Panel | What it does |
| --- | --- |
| Viewport | 3D view. Click to select; drag the gizmo to move, rotate or scale |
| Outliner | Scene tree. Click to select, drag onto another item to attach, drag to empty space to detach, right-click for actions, search box filters by name |
| Details | Name, Location / Rotation / Scale, material (colour, metallic, roughness, emission), light settings, Spin, add/remove components |
| World Settings | The level's sky (zenith, horizon and ground colours, brightness, show/hide), exposure and sun shadows; saved with the level (tab next to Details) |
| Place Actors | Add a Cube, Sphere, Spinning Cube, Empty actor, Physics Cube, Physics Sphere, Directional Light (sun), Point Light or the level's Sky & Environment |
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

### Lights and materials

Lights are actors: add a **Directional Light** (sunlight; rotate it to aim, the first one casts shadows) or a **Point Light** (a bulb with a range) from Place Actors. They show as icons in the viewport; click one to select it and edit colour, intensity and range in Details. A level without lights uses a default sun.

The sky lights the scene from all directions and shows in reflections on smooth and metallic surfaces. Change it in **World Settings**; if the level has no Environment yet, add one there or with *Place Actors > Sky & Environment*. The sun's disc appears in the sky where the first Directional Light points from.

### Physics

Press **Play** (Alt+P) and objects with a **Rigid Body** fall, collide and stack; **Stop** (Esc) puts everything back.

- *Place Actors > Physics Cube / Physics Sphere* adds a ready-made falling object 3 m above the view focus.
- Anything can take part: select it and use *+ Add Component > Rigid Body* (a **Collider** fitted to its mesh is added too). A Collider alone makes a solid obstacle that never moves, such as a floor or wall.
- In Details: body **Type** (Dynamic falls and is pushed; Kinematic follows its transform and pushes others; Static never moves), **Mass**, **Friction**, **Bounciness**, and the collider's **Shape** and size. The selected actor's collider is drawn as a green wireframe.
- `levels\physics_playground.alevel` has a wall of bricks hit by a heavy ball, balls on a ramp and a pyramid of crates. Play it in the editor, or run `aether_spinning_cubes.exe --level levels\physics_playground.alevel`.

Select a mesh to edit its material: **Metallic** (0 = plastic, wood, stone; 1 = metal), **Roughness** (0 = mirror-like, 1 = matte) and **Emissive** (glow).

### Your own models

Copy `.gltf` or `.glb` files (with their `.bin` and texture files) anywhere under `Content\` next to the editor and press **Refresh** in the Content Browser. Supported: meshes, node hierarchy, metallic-roughness materials with base colour, metallic-roughness and normal maps (PNG/JPEG), emission. Not yet: animations, skinning, occlusion and emissive maps (they are skipped with a note in the Output Log). Use `--content D:\MyModels` to browse another folder.

Levels store the model's path inside the content folder, so keep levels and the `Content` folder together when you share them. `levels/model_garden.alevel` uses the sample models.

## aether_spinning_cubes.exe — runtime demo

The same solar system running as a game would, with no editor. Esc quits. Run any level saved by the editor with `--level path\to\level.alevel` (models load from `Content\`, or `--content DIR`). Other options: `--validation`, `--no-vsync`, `--frames N`.

## Licence

Apache-2.0. Third-party licences are in `licenses/`. Verify downloads with `SHA256SUMS.txt`.
