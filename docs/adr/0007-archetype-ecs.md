# 0007. Archetype-based ECS with generational handles and UUID identity

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The World must hold millions of entities, iterate them in cache-friendly order on many threads, and give tools and AI agents a stable way to refer to entities across sessions. The task brief forbids using memory addresses as persistent identity and forbids a deep object inheritance tree.

## Decision
- **Storage:** archetypes. Entities with the same component set share an archetype; each component type is a contiguous column. Adding or removing a component moves the entity to another archetype, found through cached graph edges.
- **Handles:** `Entity` = 32-bit index + 32-bit generation. Destroying an entity bumps the generation, so stale handles fail lookups.
- **Persistent identity:** every entity has a random v4 `Uuid`, kept in a hash map. Saved data and ACP refer to UUIDs, never handles.
- **Component metadata:** type-erased `ComponentInfo` (name, size, alignment, move, destroy). Names come from `kComponentName` so they are identical on every compiler.
- **Concurrency:** queries may overlap; structural changes are deferred through `CommandBuffer`. An atomic iteration counter asserts on misuse.
- **Scheduling:** systems declare read/write sets. A system is placed one wave after the latest earlier system it conflicts with; each wave runs in parallel. Systems only see the World through a checked `SystemContext`.

## Consequences
- Iteration is fast (measured ≈ 1.8 ns per entity single-threaded, ≈ 0.5 ns with 4 threads, for a 2-component update over 1 M entities).
- Adding or removing components costs an archetype move (≈ 60 ns measured), so frequently toggled state should be a field, not a component.
- Creation pays for UUID generation and a hash insert (≈ 630 ns per entity with two components). Batched creation is a planned follow-up.
- Applying commands only after a whole schedule run means systems do not see entities spawned earlier in the same run. Stages can be added later if needed.

## Alternatives considered
- **Sparse-set ECS (EnTT style):** cheaper add/remove but slower multi-component iteration, and EnTT would be a new dependency.
- **Object hierarchy (GameObject/Actor):** rejected by the task brief; poor data locality.
- **64-bit handles as persistent IDs:** not stable across sessions or merges.
