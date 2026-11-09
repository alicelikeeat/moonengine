# Module: Gameplay

- **Layer:** 3 (depends on Foundation and World)
- **Owner role:** Gameplay maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/gameplay/`
- **Tests:** `tests/` → `aether_gameplay_tests`

Gameplay components and systems shared by every application (editor play mode, runtime, tools), with their level-file serializers.

| Type / function | Purpose |
| --- | --- |
| `Spin` | Rotates the local transform around `axis` at `speed` rad/s |
| `register_components(SerializerRegistry&)` | Level-file support for gameplay components |
| `add_systems(Schedule&, World&)` | Adds the gameplay systems to a schedule |

Future: scripting runtime, events, character controllers (see ROADMAP M4).
