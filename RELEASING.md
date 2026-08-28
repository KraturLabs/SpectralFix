# Releasing SpectralFix

GitHub releases are based on Git tags. The release workflow builds both supported
Ashita interfaces from source, runs all tests, creates two ZIP packages, generates
SHA-256 checksums, and attaches them to the GitHub release.

## First repository publication

Do not run these commands until the local v1.0 gate passes and publication is
explicitly approved.

1. Create an empty public GitHub repository named `SpectralFix` under
   `KraturLabs`. Do not initialize it with another README or license.
2. Add it as the local repository's `origin`.
3. Review the complete first commit and push `main`.
4. Confirm the GitHub `Build` workflow passes for both Ashita interfaces.

Typical commands after the empty repository exists:

```powershell
git remote add origin https://github.com/KraturLabs/SpectralFix.git
git push -u origin main
```

## Publishing v1.0

1. Confirm `PROJECT_CONTEXT.md`, `CHANGELOG.md`, and `project(... VERSION 1.0)`
   agree.
2. Run the local two-ABI build and inspect both ZIPs.
3. Require a clean working tree and passing GitHub build.
4. Create and push an annotated tag:

```powershell
git tag -a v1.0 -m "SpectralFix v1.0"
git push origin v1.0
```

5. The `Release` workflow validates the tag, rebuilds both ABIs, and creates the
   GitHub release with both ZIPs and `SHA256SUMS.txt`.
6. Download both published ZIPs, verify their hashes, and confirm each DLL reports
   the expected Ashita interface and plugin version.
7. Publish concise release notes covering installation, the two ABI choices,
   default settings, wrapper compatibility, and the no-unload requirement.

## Later releases

Update the CMake project version, plugin-facing changelog, and release notes in one
commit. Use a matching annotated `vX.Y` tag. Never rebuild or replace assets on an
existing published tag; publish a new version instead.
