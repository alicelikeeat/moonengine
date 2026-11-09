# Governance

## Commitments

These are permanent and cannot be reversed by any later governance change:

1. All code published under Apache-2.0 stays available under Apache-2.0. Already-released code will not be made proprietary.
2. Contributors keep the copyright on their contributions (DCO, no assignment).
3. The engine never requires royalties, seat fees, export fees, an account, or an online check to build, edit, run or ship a game.
4. No proprietary service or AI API becomes a required dependency.

## Roles

- **Contributor** — anyone who submits issues, code, docs or reviews.
- **Module owner** — responsible for one system (e.g. Foundation, Render, ACP): reviews changes, keeps its `MODULE.md`, interfaces and tests current. Listed in each module's `MODULE.md`.
- **Maintainer** — merge rights across the repository; triage; release management.
- **Technical Steering Committee (TSC)** — 3 to 7 maintainers who decide on RFCs, resolve disputes and approve releases. Until three maintainers exist, the project founder acts as interim TSC and must publish every decision as an issue or ADR.

Roles are earned through sustained, quality contributions. Nominations are public; maintainers decide by lazy consensus (no objection within 7 days). Inactive maintainers (6 months) move to emeritus.

## Decisions

| Change | Process |
| --- | --- |
| Bug fix, small feature, docs | Pull request + 1 approving maintainer (not the author) |
| New public API, module dependency, new third-party dependency | Pull request + module owner approval |
| Architecture, file formats, ACP protocol, licensing of dependencies | RFC (`docs/rfc/`), at least 14 days public discussion, TSC decision, recorded as an ADR |
| Governance changes | RFC + two-thirds TSC majority; cannot violate the commitments above |

Decisions prefer consensus. When consensus fails, the TSC votes by simple majority, and the reasoning is published.

## AI agents in the project

AI agents may open pull requests and review code, but:

- An agent cannot approve its own change or merge to a protected branch.
- Every agent-authored change needs approval from a human maintainer.
- Deleting data, publishing releases, accessing credentials, or changing security policy always need explicit human authorization.

## Releases

Releases are cut by a maintainer with TSC approval, tagged, and accompanied by release notes, source archive checksums, licence and dependency information, and reproducible build instructions.
