# Security Policy

## Reporting a vulnerability

**Do not open a public issue.** Report privately through GitHub: *Security → Report a vulnerability* on this repository (private security advisory).

Please include affected commit or version, reproduction steps, and impact. We will:

| Step | Target |
| --- | --- |
| Acknowledge the report | 3 business days |
| Initial assessment and severity | 10 business days |
| Fix or mitigation for high/critical issues | 90 days, coordinated with you |

We credit reporters in the advisory unless you prefer otherwise.

## Supported versions

Before 1.0, only the latest `main` receives security fixes.

## Scope

In scope: engine runtime, editor, ACP servers, asset importers, plug-in loader, build and release tooling, CI configuration.

Areas of particular concern:

- **Untrusted input.** Project files, assets, asset metadata, plug-in manifests and AI model output are untrusted data. Anything that turns them into code execution, file access outside the project, or network access is a vulnerability.
- **AI agent permissions.** Any way for an agent to exceed its granted ACP scopes, skip a transaction, or act without an audit record.
- **Script sandbox escapes** (once scripting lands). Native C++ plug-ins are trusted code by design and are not a sandbox.

## Supply chain

- Dependencies are pinned by version (and hash where the ecosystem supports it) and recorded in `third_party/dependencies.json`.
- CI uses least-privilege tokens and SHA-pinned actions.
- Never commit API keys, tokens or private assets. Rotate anything committed by accident and tell the maintainers.
- Release artifacts will ship with checksums, an SBOM and signatures, following SLSA guidance (see ROADMAP).
