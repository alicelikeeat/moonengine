# 0008. Scene hierarchy as components with two-way links

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
Scenes need parent/child relationships for transforms, editor tree views and grouped operations (move, delete, prefab instancing). The task brief asks for two-way association between the scene tree and entities, and the ECS (ADR 0007) has no built-in relationships.

## Decision
- The hierarchy is plain components: `Parent` on the child, `Children` (ordered) on the parent. Both directions are stored so tools can walk the tree either way without scans.
- Links change only through `hierarchy::set_parent` / `clear_parent` / `destroy_recursive`, which keep both sides consistent and reject cycles. `hierarchy::validate` reports any inconsistency, so tools and AI agents can check a scene after edits.
- `Transform` (local TRS) and `GlobalTransform` (world matrix) are components. `propagate_transforms` recomputes world matrices root to leaf with an explicit stack (no recursion depth limit), in parallel across root subtrees.
- Entities whose parent is missing or dead are treated as roots rather than errors, so a raw `World::destroy` never crashes propagation.
- Math conventions: right-handed, Y up, column-major matrices, quaternions as (x, y, z, w) — matching glTF and SPIR-V.

## Consequences
- Cycle checks only walk the ancestor chain when the child already has children; attaching a leaf is O(1). Building a 100,000-deep chain dropped from 517 s to under 1 s (Debug) with this rule.
- Propagation recomputes every transform each call and follows links through memory in tree order: measured ≈ 156 ns per entity single-threaded and ≈ 66 ns with 4 threads for 1 M entities in 10,000 random trees. Change detection and depth-sorted storage are planned optimisations.
- Bypassing the hierarchy functions (adding `Parent` directly) is possible but detected by `validate`.

## Alternatives considered
- **Parent pointer only:** cheap updates, but child lookup needs a full scan.
- **Built-in relationship storage in the ECS:** more intrusive; can be revisited once performance data justifies it.
