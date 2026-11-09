# 0004. ACP as the only automation surface

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
AI agents need to query and change projects. A private agent API would drift from what humans and scripts can do, and would be hard to audit.

## Decision
All automation — AI agents, editor scripts, CI — goes through the public Aether Control Protocol (`protocols/acp`). Every write is transactional with a version check and a recorded inverse. Model providers plug in behind a provider interface; no specific vendor or model is required, and "no AI" is a supported mode.

## Consequences
- The protocol must be versioned and validated in CI.
- Any capability added for agents is automatically available to scripts.

## Alternatives considered
- **Direct C++ access for agents:** no permission boundary, no audit trail.
