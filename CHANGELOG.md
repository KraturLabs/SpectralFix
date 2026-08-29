# Changelog

All notable user-facing changes are recorded here.

## 1.01 - 2026-08-29

- Document that FFXI's Blur Effect must be enabled before game startup and that
  the old `/localsettings blureffect off` workaround must be removed.
- Clarify that SpectralFix must load from Ashita's startup script rather than be
  loaded manually after login.
- Clarify that the 4.16 and 4.30 package labels identify Ashita plugin interface
  versions, not necessarily the launcher version.

## 1.0 - 2026-08-28

- Correct FFXI actor-aura edges with a verified 2048x2048 render target.
- Support native D3D8, dgVoodoo2, and atom0s D3D8-to-D3D9 paths.
- Ship separate Ashita 4.16 and 4.30 builds.
- Default to spread 2.0, stock shifted-glow opacity, and 25% center-glow opacity.
- Provide saved target, spread, shifted-opacity, and center-opacity controls.
- Detect allocation mismatches and hook conflicts and fail closed.
- Bound runtime tracking and diagnostic logs.
- Require a full client restart for installation, removal, and target changes.
