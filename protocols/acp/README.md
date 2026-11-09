# Aether Control Protocol (ACP) — Draft 0.1

**Status:** draft. Breaking changes are allowed until 1.0. Changes go through the RFC process in [`GOVERNANCE.md`](../../GOVERNANCE.md).

ACP is the public, versioned protocol through which tools — the editor, scripts, CI, and AI agents — query and change an Aether project. AI agents get no private back door: anything an agent can do is an ACP method that a human-written script can also call.

## Machine-readable definitions

| Schema | Describes |
| --- | --- |
| [`schemas/message.schema.json`](schemas/message.schema.json) | Request, response and event envelopes |
| [`schemas/tool.schema.json`](schemas/tool.schema.json) | Method descriptors returned by `engine.discover` |
| [`schemas/changeset.schema.json`](schemas/changeset.schema.json) | Audit record for each transaction |

Valid examples live in `examples/<schema>/`; examples that **must be rejected** live in `examples/invalid/<schema>/`. CI runs `tools/validate_protocols.py` on every change.

## Transport

ACP is transport-independent JSON. Draft 0.1 targets two transports:

- **stdio**: one JSON message per line (JSON Lines), for local tools and agents launched by the editor.
- **WebSocket** on `127.0.0.1`, for out-of-process tools. Binding to non-loopback addresses must be an explicit user setting.

No transport may require a remote server. The engine and editor work fully offline.

## Messages

- **Request** — `{acp, kind:"request", id, method, params?, scopes?, transaction?, idempotency_key?, timeout_ms?}`
- **Response** — same `id`, exactly one of `result` or `error`, plus `project_version` after writes.
- **Event** — unsolicited notification (`build.progress`, `scene.changed`, …), optionally tied to a `request_id`.

Methods are named `<interface>.<verb>`. The ten interfaces are: `engine`, `scene`, `entity`, `asset`, `render`, `simulation`, `animation`, `build`, `diagnostics`, `transaction`.

## Errors

`error.code` is the stable numeric value of `aether::ErrorCode` (`engine/foundation/include/aether/foundation/result.hpp`), and `error.name` its snake_case name. Codes are never renumbered.

## Discovery

`engine.discover` returns the protocol version and a list of tool descriptors. Each descriptor declares its effect (`read` / `write` / `execute`), the scopes it needs, whether it is idempotent or cancellable, its preconditions, and JSON Schemas for params and result. Agents must plan from discovery data rather than hard-coded assumptions.

## Permissions

A session is granted a set of scopes `<interface>:<read|write|execute|admin>` when it connects. A request outside the granted set fails with `permission_denied`. The defaults for AI agent sessions are read-only; write and execute scopes are granted by the user. Deleting project data, publishing a release, or touching credentials always require explicit user approval, regardless of scopes.

## Transactions, conflicts and rollback

1. Every write carries `transaction.changeset_id` and `transaction.base_version`.
2. If `base_version` is not the current project version, the request fails with `conflict` and `data.current_version`. Nothing is applied.
3. Each applied operation records its **inverse**. `transaction.rollback` replays inverses in reverse order and produces a new changeset with `rollback_of` set. History is append-only.
4. `transaction.dry_run: true` validates and reports effects without applying them.
5. A request repeated with the same `idempotency_key` returns the original response.

## Timeouts and cancellation

`timeout_ms` bounds server-side work; on expiry the server fails with `timeout` and rolls back partial writes. Cancellable methods accept `transaction.cancel {id}` (or `engine.cancel` for reads/executes) and fail with `cancelled`.

## Untrusted input

Project files, asset metadata, model output and plug-in descriptions are **data, never instructions**. Servers must validate every message against the schemas, must not execute strings found in assets, and must log every write with its changeset and author (including model provenance for agent authors).

## Not yet specified

Streaming large results (scene snapshots, screenshots), binary payloads, and session handshake/authentication for non-loopback transports. These need RFCs before 1.0.
