# Testing and verification

## Automated checks

Both Ashita ABIs compile as 32-bit Release builds with C++20 and `/W4 /WX`.
The current matrix contains seven CTest jobs per ABI.

The test suite verifies:

- Export presence, Ashita interface identity, plugin creation, and plugin version.
- Downsample, tap-spread, opacity, center-geometry, and half-pixel rewrites at
  1024, 2048, and 4096.
- Ordinal-1 startup selection, activity confirmation, mismatch handling, and
  client-specific selector validation.
- The spectralfix.ini round trip, parsing of existing v1.01 files including CRLF
  and inline comments, settings-only persistence before a selector is learned,
  rejection of out-of-range and negative values, and silent tolerance of unknown
  keys.
- Hook table decisions: displacement detection, deliberately released slots, and
  the draw-chain watchdog's activity, quiet-window, confirmed-loss, and directly-
  owned-slot decisions. Unknown late owners are preserved rather than overwritten.
- Runtime safety policy through a fake vtable: complete publication, successful
  rollback, write failure after partial publication, rollback failure with accurate
  retained ownership, conventional forwarding-hook preservation, shared-vtable
  secondary-device rejection, one-shot failure notices, and release policy before
  and after enlargement.
- Allocation-context policy: only the Ashita device with a nonzero FFXiMain stack
  identity can participate in candidate selection.
- Center-composite state capture, application, and restoration, including full
  restoration while a faulting draw unwinds and after a partially applied state
  change.

The per-feature correction gates, continued scheduling of the draw-chain watchdog,
and release refusal still need `IAshitaCore` and a live device. Their pure policy
decisions are host-tested where possible; the callback lifecycle itself requires
in-game verification.

The earlier settings, hook-policy, and composite-state safety suites were mutation
checked: reverting the fix each covers makes it fail with the specific message for
that case. The fake-vtable suite directly injects publication and rollback failures.

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
  adjustment, verified selector, no hook displacement or failures. Its launcher ended
  the process without invoking the plugin release callback; process exit removed
  all process-owned state.
- Horizon Ashita 4.30 plus atom0s and XIUI: correct avatar rendering, live spread
  adjustment, dismissal, zoning, resummon, zero failures, and clean restoration of
  all three hooks.
- Native Windows D3D8: the compatibility branch previously passed Lion II summon,
  dismissal, zoning, resummon, and XIUI coexistence without a top-left copy.

## Blur Effect prerequisite

FFXI's Blur Effect must remain enabled while SpectralFix is in use. An earlier
v1.0 off-at-start test created only one candidate and did not recover until the
client was relaunched; that result could not be reproduced with the hardened
v1.02 build.

For v1.02, both the standalone local Ashita 4.30 client and Horizon Ashita 4.30
were launched after Blur Effect was disabled and persisted through a full client
exit. Both created the normal two candidates but produced no aura draws. Running
`/localsettings blureffect on` in those same sessions immediately produced the
visually corrected aura, selector confirmation, and nonzero scale/tap counters.
The public instruction therefore enables Blur Effect first and recommends one
full relaunch only if the corrected aura does not resume.

Because the Ashita SDK does not expose the live toggle, SpectralFix does not try
to detect or warn about it. Runtime inactivity and startup commands are not
treated as proof of the persisted local setting.

## v1.02 runtime validation

The completed v1.02 runtime matrix is intentionally summarized here; detailed
counters remain in the local verification log.

| Path | Coverage | Result |
|---|---|---|
| Ashita 4.30 + dgVoodoo2 | Saved/fresh config, Abyssal Maw, summon, dismissal, zone, resummon | Passed visually with zero failures or correction loss |
| Native Windows D3D8 | Native stage-zero queries, summon lifecycle, zoning | Passed with zero failures or correction loss |
| Ashita 4.16 + dgVoodoo2 + XIUI | Lion II, saved settings, summon lifecycle, zoning | Passed with zero failures or correction loss |
| Foreign DrawPrimitiveUP owner | Conventional late hook preserved as owner; forwarding then deliberately stopped | Passed: no re-chain or crash while forwarding; three quiet windows produced the warning and `correction_lost=true` |
| Direct unload and `UnloadAll()` | Separate live enlarged-allocation sessions | Correction retained and both clients exited cleanly |

FFXI creates candidate 1 before a helper can schedule a truly pre-enlargement live
unload, so that exact live branch remains unexercised. The very-early unload attempt
arrived after enlargement and correctly used the safe refusal path.

## Historical standalone v1.0 release-candidate verification

- Both pinned SDK builds complete as 32-bit Release with `/W4 /WX`.
- At the v1.0 tag, all three tests then present passed for both Ashita interfaces.
- Export smoke reports interface 4.16 or 4.30 as appropriate and plugin version 1.0.
- Both ZIPs contain only `spectralfix.dll`, `LICENSE.txt`, `INSTALL.txt`, and
  `BUILD.txt`.
- The Ashita 4.30 wrapper utility loads the local dgVoodoo2 `d3d8.dll` and calls
  `Direct3DCreate8` successfully.

## Historical v1.0 release boundary

The local, CatsEye, and Horizon runtime regressions above were the accepted v1.0
release evidence. A separate 30-60 minute soak was not required. A coordinated
50-aura crowd test is not practical and remains a documented limit rather than a
release blocker.
