# Architecture

## Why the artifact occurs

FFXI creates a 256x256 A8R8G8B8 render target for actor auras, renders the actor
silhouette into it, and composites several shifted copies. At modern background
resolutions that small target is enlarged enough to expose stair-stepped edges.
The tap offsets are also fixed pixel values, so the apparent glow becomes too
narrow as resolution increases.

This is client-generated rendering. No DAT texture controls the target size or
the composite geometry.

## Correction path

SpectralFix hooks three methods on the actual `IDirect3DDevice8` instance:

1. `CreateTexture` identifies the normalized aura-allocation signature and
   enlarges only the selected ordinal.
2. `SetTexture` passively records when the exact selected aura resource is bound
   to stage zero. Native Windows D3D8 can switch to a narrow `GetTexture` query
   when another component repeatedly restores this observer.
3. `DrawPrimitiveUP` corrects the selected downsample and shifted-tap geometry.
   It also recognizes FFXI's separate centered hard-cutout pass and applies the
   configured center opacity with full render-state restoration.

The plugin never substitutes a render target or texture. It changes the selected
allocation dimensions and the matching client draw data.

## Allocation selection

Across the tested clients, the aura is ordinal 1 of one normalized FFXiMain
allocation signature. A fresh install optimistically enlarges that ordinal and
then verifies it from aura-specific activity. If another candidate proves to be
the aura, SpectralFix saves the corrected selector for that client build, disables
correction, and requires a full client exit.

Private data markers are attached to both the selected texture and its level-zero
surface. Wrapper paths can use exact dimension fallback only after a selector is
valid. Native stock-sized textures never use a broad dimension-only match.

Ashita does not expose the live Blur Effect toggle through its public plugin SDK.
SpectralFix deliberately does not infer it from missing draw activity or startup
commands because neither proves the persisted local setting. Blur Effect remains
a documented manual prerequisite rather than a runtime health signal.

## Hook coexistence and shutdown

Hook installation is transactional. SpectralFix publishes no enlarged allocation
unless all required hooks are installed. Every rollback write is verified; if a
partial install cannot be restored exactly, SpectralFix preserves the ownership it
can prove, stays resident when necessary, and requires a client restart. It checks
vtable ownership every sixty presented frames. A newly installed owner is left in
place because it may have saved SpectralFix as its previous function; installing
SpectralFix above it could create a recursive forwarding cycle. Unknown ownership
therefore fails closed.

When another component remains above SpectralFix's DrawPrimitiveUP hook, one
successful sample does not permanently prove the chain is safe. SpectralFix keeps
watching intercepted draws every sixty presented frames. Activity resets the
watchdog; three consecutive windows without a draw produce an immediate-exit
warning. If SpectralFix still owns the draw slot directly, no sampling verdict is
needed.

The three slots are held in one table, and displacement plus draw-chain watchdog
decisions live in `hook_policy.hpp` without touching memory, so they are unit
tested. A slot handed back on purpose, such as the SetTexture observer released to
native Windows D3D8, is marked untracked and skipped by later ownership checks.

Candidate allocations must come from the exact device Ashita supplied and must
have a nonzero FFXiMain stack identity. Matching allocations from another shared-
vtable device, or from a call path that cannot be tied to FFXiMain, remain stock.

Once hooks are published, the SpectralFix module is pinned for the process
lifetime. This keeps its passthrough code mapped even if another component retains
SpectralFix in a hook chain. In-process unload and reload are deliberately refused;
users must exit the client.

## Failing without corrupting the frame

Correction is gated per feature rather than by one switch. Publishing new enlarged
allocations, and the tap and center-composite adjustments, can each stop
independently.

Correcting the downsample geometry of an allocation SpectralFix already enlarged
is deliberately not one of them. That allocation is live and client-owned, and its
draws are still authored in 256-pixel space; passing them through untouched is
exactly what puts an oversized animated copy on screen. So once an enlargement has
been published, that correction continues for the life of the process, including
after a hook conflict or an allocation mismatch has stopped everything else.
SpectralFix only goes fully inert when it never published an enlargement at all.

## Unload and release

`IPluginManager::Unload` and `UnloadAll` are available to every Ashita component, so
`Release` can arrive mid-session through a route the `/unload` command guard cannot
see. When an enlarged allocation is live, SpectralFix refuses the release: it keeps
its hooks, stays resident, and the export that would destroy the instance leaks it
deliberately, because the hook functions in the device vtable still reference it.
Draw correction is retained for shutdown, but Present callbacks stop, so status,
settings updates, and hook-displacement monitoring stop with them. The user is told
to exit the entire client immediately rather than continue playing in that degraded
state. At genuine client shutdown the retained process-owned state costs nothing.

## Resource and state safety

- Every acquired texture, surface, depth surface, and render target reference is
  released on all return paths.
- Center-opacity changes snapshot and restore all affected render and texture
  stage states around one draw, through a scope guard that also restores while a
  faulting draw unwinds. The plugin is built with `/EHa`, so `catch (...)` in the
  hook callbacks catches access violations raised inside client vertex data; the
  guard is what keeps that recovery from leaving modified state behind.
- Hook callbacks use thread-local reentrancy guards and preserve the original draw
  when correction cannot be applied. Repeated callback or corrected-draw failures
  retain their counters but queue only one user-facing warning per failure class.
- Signature and activity tracking are bounded at 64 and 256 entries.
- The diagnostic log rotates at 4 MiB with one previous archive. If it cannot be
  opened, rotated, or written, file logging stops for the session rather than
  growing without bound or retrying every frame. This costs troubleshooting output
  only; it never stops the fix from loading.
- Requested target size is checked against D3D8 device capabilities before hooks
  are installed.

## Public defaults

- Target: 2048x2048
- Spread: 2.0
- Shifted-tap opacity: stock
- Center-composite opacity: 25%

The 1024 target visibly restores some jaggedness. The 4096 target increases target
memory and pixel work by four times over 2048 without a meaningful improvement in
the initial visual comparison. Both remain explicit user choices.
