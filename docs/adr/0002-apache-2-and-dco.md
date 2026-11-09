# 0002. Apache-2.0 licence with DCO sign-off

- **Status:** Accepted
- **Date:** 2026-11-09

## Context
The engine must be free to use, modify and ship commercially, with no royalties, and contributors must keep their copyright.

## Decision
License all project code under Apache-2.0. Contributions are accepted under the Developer Certificate of Origin 1.1 (`git commit -s`); there is no copyright assignment and no CLA.

## Consequences
- Apache-2.0 includes an explicit patent grant and is compatible with most permissive third-party code.
- GPL-licensed code cannot be linked into the engine core. Third-party dependencies must be reviewed against `third_party/README.md`.
- Because copyright stays with contributors, the project cannot later relicense existing code as proprietary.

## Alternatives considered
- **MIT:** no patent grant.
- **MPL-2.0 / GPL:** copyleft obligations discourage use in shipped games.
