# Releasing

Releases are built by GitHub Actions from a tagged commit, so every binary is traceable to public source and a public build log. Nobody uploads binaries from their own machine.

## Steps

1. Make sure `main` is green.
2. Set `VERSION` in the top-level `CMakeLists.txt` to the new version (`X.Y.Z`).
3. Move the `[Unreleased]` entries in `CHANGELOG.md` under a new `[X.Y.Z] - YYYY-MM-DD` heading.
4. Write release notes in `docs/releases/vX.Y.Z.md` (download steps, highlights, known limitations).
5. Merge those changes to `main`, then tag and push:
   ```bash
   git tag -a vX.Y.Z -m "Aether Engine vX.Y.Z"
   git push origin vX.Y.Z
   ```
   Alternatively run the **Release** workflow by hand from the Actions tab with the tag name; it creates the tag on the selected branch.
6. `.github/workflows/release.yml` then:
   - checks the tag matches `VERSION` and that release notes exist,
   - builds and tests the Windows Release configuration,
   - packages the demo with `LICENSE`, `NOTICE` and third-party licences,
   - attaches the zip, the dependency manifest and `SHA256SUMS.txt` to a new GitHub Release.

Versions below 1.0 are published as pre-releases.

Per `GOVERNANCE.md`, a release needs TSC approval; until the TSC exists, the project owner approves.

## Not yet done

Code signing, provenance attestations (SLSA) and a CycloneDX/SPDX SBOM are tracked in `ROADMAP.md` (M7).
