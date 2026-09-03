# Changelog

All notable user-facing changes are recorded here.

## 1.03 - 2026-09-03

- Accept evidence-backed allocation fallbacks when FFXiMain supplies only a direct
  caller or only a usable stack fingerprint, and recognize alternate interface
  pointers only when a bounded, retained canonical COM identity cache proves they
  are the same device. Multiple method-specific aliases are supported without
  repeated hot-path `QueryInterface` calls.
- Keep existing version-1 selectors compatible while classifying exact,
  caller-only, stack-only, conflicting, module, and ordinal match outcomes. A weak
  saved selector that encounters stronger identity evidence stays stock, learns
  the stronger selector from activity, and requires a restart; one session-bound
  candidate ID prevents two compatible candidates from being enlarged or confirmed.
- Track CreateTexture, SetTexture, and DrawPrimitiveUP capabilities separately.
  SetTexture loss uses a narrow stage-zero query; new enlargement still requires
  directly owned or recently observed DrawPrimitiveUP correction. If stage-zero
  identity is unavailable, selector activity, stronger-identity learning, and
  required downsample correction continue while optional appearance work stays off.
- Preserve unknown foreign hook owners without re-chaining. Forwarding capability
  can recover after current evidence appears and expires under the existing quiet
  watchdog when the chain stops calling SpectralFix. Callback counts are diagnostic;
  only draws from the verified runtime device can refresh or recover forwarding.
- Expand bounded diagnostics with Windows/Wine environment, device and allocation
  evidence, selector match strength, hook owners/capabilities, and module-pinning
  method/error details. Wine compatibility remains experimental pending live tests.

## 1.02 - 2026-08-29

- Keep every live enlarged aura safely corrected through selector mismatches, plugin
  conflicts, direct unload attempts, and `UnloadAll`. Unsafe unloads now retain the
  correction only for shutdown and clearly tell the player to exit the client.
- Improve coexistence with native D3D8, dgVoodoo2, and later graphics hooks.
  Unknown hook owners are monitored in place, avoiding recursive hook chains while
  warning if the correction path is genuinely lost.
- Restrict aura selection to Ashita's exact Direct3D device and a verified FFXiMain
  allocation path, preventing unrelated matching resources from being enlarged.
- If a graphics operation fails, put the game's graphics settings back exactly as
  they were. If SpectralFix can no longer prove a texture belongs to the aura, keep
  the required correction but stop optional appearance adjustments.
- Make settings and startup more reliable: create missing folders, save appearance
  settings before the aura allocation is learned, reject malformed values, tolerate
  settings added by future versions, and keep working when the log is unavailable.
- Prevent failure spam and unsafe partial setup: show one clear warning for a
  repeated rendering problem, keep log files bounded, safely track or undo partially
  installed hooks, and cover those failure paths with automated tests.

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
