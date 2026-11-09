# Contributing to Aether Engine

Thank you for helping. Contributions of all sizes are welcome: bug reports, docs, tests, code, benchmarks and reviews.

## Ground rules

- Be respectful. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
- Keep the engine fully open: no closed-source runtime dependencies, no mandatory online services, no proprietary AI APIs as requirements.
- Results over claims. A change is done only when it builds, passes tests, and others can reproduce it.

## Developer Certificate of Origin (DCO)

We use the [Developer Certificate of Origin 1.1](https://developercertificate.org/) instead of a CLA. **You keep your copyright.** By signing off you certify that you have the right to submit the work under the project licence.

Sign off every commit:

```bash
git commit -s -m "foundation: add arena rewind"
```

This adds `Signed-off-by: Your Name <you@example.com>`, matching your git author. CI rejects pull requests with unsigned commits. To fix a branch: `git rebase --signoff main`.

## Workflow

1. For anything non-trivial, open an issue first. Architectural changes, new public APIs, file-format or protocol changes need an [RFC](docs/rfc/0000-template.md).
2. Fork, branch from `main`, make focused commits.
3. Build and test locally (see [docs/building.md](docs/building.md)), including a sanitizer build for threading or memory code.
4. Open a pull request using the template. CI must be green.
5. At least one maintainer who did not write the change must approve. Changes to Foundation, ACP or security-sensitive code need a module owner.

## Definition of done

1. Compiles on every platform it claims to support, with warnings as errors.
2. Public interfaces match the documented protocol version.
3. Unit / integration / regression tests pass. **Never delete or weaken an assertion to make a test pass.**
4. Performance-relevant changes include before/after numbers and the machine they came from.
5. New dependencies have a licence review (see `third_party/README.md`).
6. Every public API is documented.
7. Existing projects and asset formats keep working, or a migration is provided.
8. AI-generated code is reviewed by a human and by someone other than its author.
9. Others can reproduce the result.

## Code style

- C++20, no compiler extensions. Format with the repository `.clang-format`.
- Report recoverable errors with `aether::Result<T>`; use `AETHER_ASSERT` for programming errors.
- No global singletons for module communication (ADR 0003). Pass dependencies explicitly.
- Public headers go in `include/aether/<module>/`. Everything in `src/` is private.
- Comments explain *why*, not *what*.

## Commit messages

`<module>: <imperative summary>` on the first line (≤ 72 characters), blank line, then the reasoning.

## Using AI tools

AI assistance is welcome. You are responsible for every line you submit: understand it, test it, and make sure it does not contain code you are not allowed to license under Apache-2.0. Mention significant AI assistance in the pull request description.

## Recognition

Every contributor appears in the git history and in release notes. Copyright notices name "The Aether Engine Contributors"; individual copyright remains with each author.
