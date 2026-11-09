# 0003. Explicit dependencies instead of global singletons

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
Global singletons hide dependencies, make tests interfere with each other, and prevent running two engine instances in one process (needed for editor + play-in-editor, and for parallel agent sandboxes).

## Decision
Systems receive their collaborators (allocator, logger, job system, trace collector) through constructors. Foundation has exactly two process-wide items: the assertion handler and the thread index counter.

## Consequences
- Constructors are a little longer; an `EngineContext` struct will bundle common services once World lands.
- Tests create isolated instances freely.

## Alternatives considered
- **Service locator:** still global; failures move from compile time to run time.
