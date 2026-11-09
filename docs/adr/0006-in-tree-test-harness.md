# 0006. In-tree test harness, no third-party dependencies in Foundation

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The supply chain starts empty. Every dependency needs licence review, version pinning and vulnerability tracking.

## Decision
Foundation and its tests use only the C++ standard library and OS APIs. Tests use a ~150-line harness in `tests/support`. Higher layers may add reviewed dependencies (e.g. Vulkan headers, a physics library) through `third_party/`.

## Consequences
- Builds work offline with no package manager.
- The harness lacks features like parameterised tests; revisit if they are needed.

## Alternatives considered
- **GoogleTest / Catch2 / doctest:** good options, but a dependency we do not need yet.
