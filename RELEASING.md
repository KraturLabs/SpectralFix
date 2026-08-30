# Releasing SpectralFix

GitHub releases are based on Git tags. The release workflow builds both supported
Ashita interfaces from source, runs all tests, creates two ZIP packages, generates
SHA-256 checksums, and attaches them to the GitHub release.

## Release checklist

Do not publish, tag, or replace release assets without explicit approval.

1. Confirm `PROJECT_CONTEXT.md`, `CHANGELOG.md`, and the CMake project version
   agree on the intended release.
2. Run `scripts\Build-All.ps1` against both pinned SDK revisions. Require every
   CTest job, `/W4 /WX`, export smoke, and package generation to pass.
3. Inspect both ZIPs. They must contain only `spectralfix.dll`, `LICENSE.txt`,
   `INSTALL.txt`, and `BUILD.txt`.
4. Review and commit the exact source, test, and documentation diff, then push it.
5. Confirm the GitHub `Build` workflow passes for both Ashita interfaces.
6. Create and push a matching annotated tag, substituting the approved version:

```powershell
git tag -a vX.Y -m "SpectralFix vX.Y"
git push origin vX.Y
```

7. The `Release` workflow validates the tag, rebuilds both ABIs, and creates the
   GitHub release with both ZIPs and `SHA256SUMS.txt`.
8. Download both published ZIPs, verify their hashes, and confirm each DLL reports
   the expected Ashita interface and plugin version.
9. Publish concise release notes covering installation, the two ABI choices,
   default settings, wrapper compatibility, and the no-unload requirement.

## Later releases

Use a new matching `vX.Y` tag for every release. Never rebuild or replace assets on
an existing published tag; publish a new version instead. A failed or superseded
workflow run may remain in history, but the exact tagged commit and its replacement
build must be green before publication is treated as complete.
