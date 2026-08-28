# Testing and verification

## Automated checks

Both Ashita ABIs compile as 32-bit Release builds with C++20 and `/W4 /WX`.

The test suite verifies:

- Export presence, Ashita interface identity, plugin creation, and plugin version.
- Downsample, tap-spread, opacity, center-geometry, and half-pixel rewrites at
  1024, 2048, and 4096.
- Ordinal-1 startup selection, activity confirmation, mismatch handling, and
  client-specific selector validation.

`spectralfix_wrapper_smoke` is an optional developer utility that loads a supplied
32-bit `d3d8.dll` and calls `Direct3DCreate8`. dgVoodoo2 supports this standalone
check; the tested atom0s proxy initializes only inside FFXI.

## Pinned SDK matrix

| Ashita interface | Official `AshitaXI/Ashita-v4beta` revision |
|---|---|
| 4.16 | `a362f9e3594c7ba8e9e3108b77b0033230ee1373` |
| 4.30 | `2e4b9c86de538ecfedabab918537c550d6378aaa` |

These revisions exactly match the `Ashita.h` files in the two tested client SDKs.

## Runtime evidence before standalone extraction

- Local Ashita 4.30 plus dgVoodoo2: correct first-launch selection, summon,
  dismissal, resummon, zoning, settings, center opacity, and clean hook release.
- CatsEye Ashita 4.16 plus dgVoodoo2: correct Lion II rendering, live spread
  adjustment, verified selector, no hook re-chains or failures. Its launcher ended
  the process without invoking the plugin release callback; process exit removed
  all process-owned state.
- Horizon Ashita 4.30 plus atom0s and XIUI: correct avatar rendering, live spread
  adjustment, dismissal, zoning, resummon, zero failures, and clean restoration of
  all three hooks.
- Native Windows D3D8: the compatibility branch previously passed Lion II summon,
  dismissal, zoning, resummon, and XIUI coexistence without a top-left copy.

## Standalone v1.0 release-candidate verification

- Both pinned SDK builds complete as 32-bit Release with `/W4 /WX`.
- All three CTest jobs pass for both Ashita interfaces.
- Export smoke reports interface 4.16 or 4.30 as appropriate and plugin version 1.0.
- Both ZIPs contain only `spectralfix.dll`, `LICENSE.txt`, `INSTALL.txt`, and
  `BUILD.txt`.
- The Ashita 4.30 wrapper utility loads the local dgVoodoo2 `d3d8.dll` and calls
  `Direct3DCreate8` successfully.

## Release decision

The user accepted the existing local, CatsEye, and Horizon runtime regressions as
sufficient for v1.0 and chose not to require a separate 30-60 minute soak. A
coordinated 50-aura crowd test is not practical and remains a documented limit
rather than a release blocker.
