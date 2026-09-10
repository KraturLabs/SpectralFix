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

SpectralFix hooks three methods on the active `IDirect3DDevice8` instance:

1. `CreateTexture` identifies the aura allocation and enlarges only the selected
   candidate.
2. `SetTexture` records when that exact aura resource is bound to stage zero. If
   another component owns the slot, SpectralFix preserves it and uses a narrow
   `GetTexture(0)` query only on already-classified aura geometry.
3. `DrawPrimitiveUP` corrects the enlarged target's downsample and shifted-tap
   geometry and applies the configured center opacity with render-state
   restoration.

The plugin does not replace DAT files, textures, or render targets. It changes the
selected allocation dimensions and the matching client draw data.

## Allocation selection

Across tested clients, the aura is ordinal 1 of one normalized FFXiMain allocation
signature. A fresh install enlarges that candidate and verifies it from aura-specific
activity. If another candidate proves to be the aura, SpectralFix saves the corrected
selector, stops new enlargement, keeps any correction required by an already-live
enlarged allocation, and requires a full client exit.

Markers are attached to the selected texture and its level-zero surface. Wrapper
paths may use exact dimension fallback only after a selector is valid. Stock-sized
textures never use a broad dimension-only match.

Raw device-pointer equality is the strongest identity path. Alternate interface
pointers may be accepted when canonical COM identity proves they refer to the same
device. At least one FFXiMain source is required: the direct caller RVA, the bounded
stack hash, or both. If neither is available, the allocation remains stock.

Saved selectors retain the FFXiMain timestamp, image size, allocation ordinal,
caller RVA, and stack hash. Caller-only and stack-only selectors are valid. Exact
matching is preferred when both fields are available; one unavailable field permits
a caller or stack fallback. If stronger identity evidence appears later, SpectralFix
can learn it for the next full launch instead of broadening the current match.
Conflicting identity, client-build changes, ordinal changes, and selectors with no
FFXiMain identity are rejected and logged by reason.

Ashita does not expose the live Blur Effect toggle through its public plugin SDK,
so SpectralFix does not infer it from missing draw activity. Blur Effect remains a
manual prerequisite.

## Hook coexistence and shutdown

Hook installation is transactional. SpectralFix publishes no enlarged allocation
unless all required hooks are installed. Rollback writes are verified; if a partial
install cannot be restored exactly, SpectralFix keeps only ownership it can prove
and requires a client restart.

Unknown later hook owners are preserved rather than overwritten. A foreign owner
may already forward into SpectralFix, and writing SpectralFix back above it could
create a recursive hook chain. Per-slot capability tracking determines what remains
safe to do when ownership changes.

A displaced `SetTexture` observer can use the narrow stage-zero query fallback.
`CreateTexture` observation may continue through a forwarding owner. New enlargement
requires `DrawPrimitiveUP` correction to be directly owned or recently observed
forwarding into SpectralFix from the verified runtime device.

Forwarding evidence is periodically rechecked and expires after repeated quiet
samples. If a live enlarged allocation loses its required correction path,
SpectralFix warns the user to exit rather than pretending the session is safe.

Once hooks are published, the SpectralFix module is pinned for the process lifetime
with `GetModuleHandleEx(PIN | FROM_ADDRESS)`. If pinning fails, hooks are left
unpublished because a hook target must not outlive the DLL that owns its code.
In-process unload and reload are therefore deliberately unsupported; users must
exit the client.

## Failing without corrupting the frame

Correction is gated per feature rather than by one global switch. Publishing new
enlarged allocations and optional appearance changes can stop independently.

Downsample correction for an allocation SpectralFix already enlarged is different:
that correction must continue for the life of the process. Passing the enlarged
resource through stock 256-pixel draw coordinates is what produces the oversized
copy artifact. SpectralFix only becomes fully inert when it never published an
enlargement.

## Unload and release

Ashita can invoke plugin release while the process is still running. If an enlarged
allocation is live, SpectralFix keeps the required hooks and instance resident and
requires a full client exit. At normal process shutdown the retained process-owned
state disappears with the process.

## Resource and state safety

- Acquired texture, surface, depth-surface, and render-target references are
  released on all normal return paths.
- Center-opacity changes snapshot and restore affected render and texture-stage
  state around the draw.
- Hook callbacks use thread-local reentrancy guards and preserve the original draw
  when correction cannot be applied.
- Signature and activity tracking are bounded.
- Diagnostic logs are size-limited and logging failure does not disable the fix.
- Requested target size is checked against D3D8 device capabilities before hooks
  are installed.

## Public defaults

- Target: 2048x2048
- Spread: 2.0
- Shifted-tap opacity: stock
- Center-composite opacity: 25%

1024 can reintroduce visible jaggedness. 4096 increases memory and pixel work
substantially over 2048 without a meaningful visual improvement in the tested
comparison, so 2048 remains the default.
