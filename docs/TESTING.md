# Testing and verification

## Automated checks

Both supported Ashita ABIs compile as 32-bit Release builds with C++20 and
`/W4 /WX`. The current matrix contains eight CTest jobs per ABI.

The test suite covers:

- Plugin exports, interface identity, creation, and version.
- Aura geometry rewrites at 1024, 2048, and 4096 targets.
- Selector matching, ordinal selection, caller-only and stack-only identity,
  stronger-identity relearning, mismatch handling, and one-candidate-per-session
  binding.
- Settings parsing, persistence, validation, and compatibility with existing
  version-1 configuration files.
- Hook ownership, displacement, forwarding evidence, recovery, rollback, and
  release policy.
- Device alias identity and bounded COM reference handling.
- Render-state capture and restoration, including failure paths.

Some lifecycle behavior requires a real FFXI/Ashita process and Direct3D device,
so pure policy is host-tested while callback lifecycle is verified in game.

## Pinned SDK matrix

| Ashita interface | Official `AshitaXI/Ashita-v4beta` revision |
|---|---|
| 4.16 | `a362f9e3594c7ba8e9e3108b77b0033230ee1373` |
| 4.30 | `2e4b9c86de538ecfedabab918537c550d6378aaa` |

These revisions match the `Ashita.h` files used for the supported builds.

## Runtime compatibility coverage

The release path has been exercised on:

- Ashita 4.30 with dgVoodoo2.
- Ashita 4.16 with dgVoodoo2 and XIUI.
- Native Windows D3D8.
- Horizon with the atom0s D3D8 wrapper.

Coverage includes fresh and saved selectors, summon/dismiss/resummon, zoning,
settings changes, wrapper coexistence, hook ownership changes, and full-client
shutdown.

Wine support is experimental until tested in a live Wine-hosted FFXI/Ashita
process. The plugin does not intentionally block Wine: caller-only or stack-only
FFXiMain identity is supported, alternate device pointers may be accepted through
canonical COM identity, and the Windows DLL is expected to run inside the same
32-bit Ashita process under Wine.

## Blur Effect prerequisite

FFXI's Blur Effect must remain enabled while SpectralFix is in use. Ashita does
not expose that setting through the public plugin SDK, so SpectralFix does not try
to infer it from missing draw activity. If the aura remains jagged or absent, run:

```text
/localsettings blureffect on
```

If the corrected aura still does not appear, fully exit FFXI and relaunch once.

## Manual compatibility check

For a new wrapper or Wine environment:

1. Start with a fresh `spectralfix.ini` and load SpectralFix from the startup
   script with Blur Effect enabled.
2. Summon an aura, dismiss it, summon it again, and zone once.
3. Run `/spectralfix status` before and after the aura test and keep the complete
   `spectralfix.log` from initialization through the test.
4. If SpectralFix learns a selector, fully exit the client and repeat once with
   the saved configuration.

A compatibility failure should be investigated from the recorded identity,
selector, hook-owner, and capability information rather than by broadly weakening
runtime checks.
