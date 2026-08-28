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

## Hook coexistence and shutdown

Hook installation is transactional. SpectralFix publishes no enlarged allocation
unless all required hooks are installed. It checks vtable ownership once per
second and can transactionally re-chain above a newly installed owner up to three
times. Unknown owners, repeated displacement, or displacement of a required hook
fail closed.

Once hooks are published, the SpectralFix module is pinned for the process
lifetime. This keeps its passthrough code mapped even if another component retains
SpectralFix in a hook chain. In-process unload and reload are deliberately refused;
users must exit the client.

## Resource and state safety

- Every acquired texture, surface, depth surface, and render target reference is
  released on all return paths.
- Center-opacity changes snapshot and restore all affected render and texture
  stage states around one draw.
- Hook callbacks use thread-local reentrancy guards and preserve the original draw
  when correction cannot be applied.
- Signature and activity tracking are bounded at 64 and 256 entries.
- The diagnostic log rotates at 4 MiB with one previous archive.
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
