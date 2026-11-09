# Third-party dependencies

The engine currently has **no third-party source or binary dependencies**. Foundation uses only the C++ standard library and OS APIs (ADR 0006).

Developer tooling outside the shipped engine:

| Tool | Version | Licence | Used for |
| --- | --- | --- | --- |
| jsonschema (Python) | 4.23.0 | MIT | Validating ACP protocol files in CI |

## Adding a dependency

1. Open an issue explaining why it is needed and what alternatives exist.
2. Licence must be Apache-2.0 compatible (MIT, BSD, Zlib, Apache-2.0, Boost, ISC, …). No GPL/AGPL in anything linked into the engine or editor. No non-redistributable or "source-available" code.
3. Pin an exact version or commit, with a SHA-256 of the source archive.
4. Add an entry to `dependencies.json` (name, version, licence, upstream URL, SHA-256, which modules use it) and copy its licence text to `third_party/licenses/<name>.txt`.
5. Module owner and one maintainer approve.

`dependencies.json` is the source for the SBOM generated at release time.
