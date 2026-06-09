# Total Overdose RTX Remix Support Design From Ghidra

Date: 2026-06-02

Scope: derive the correct RTX Remix support boundary from `TOD.exe` behavior, not
from trial-and-error texture logging. The live ASI/source should remain the exact
known-good baseline from `C:\GOG Games\TotalOverDoseRTXRemix` while this design is
used.

## Protected Baseline

- Live ASI restored from reference:
  `scripts\TODCameraResend.asi`, 132608 bytes, timestamp `2026-06-01 06:27:35`.
- Live source restored from reference:
  `tools\tod_camera_resend_asi\TODCameraResend.cpp`, 65867 bytes, timestamp
  `2026-06-01 06:27:30`.
- Do not add runtime texture hashing, candidate/discovery `LockRect` reads, dirty
  refreshes, draw-time texture substitution, or candidate dumping to the shipping
  ASI. The known-good generic managed-texture copy path already locks supported
  managed textures once to build DEFAULT-pool copies; do not extend that into HUD
  hash/readback/refresh logic. The failed 151552-byte lean UI attempt proved even
  scoped texture reads/refreshes can break mission 3 RTX.

## Ghidra Facts

### D3D9 Backend

- `Direct3DCreate9` thunk: `0099e9f2`.
- Adapter initialization: `FUN_0045e620`.
- Device creation: `FUN_00451110`.
- Adapter singleton/global: `DAT_00a39f14`.
- Adapter offset `0x00`: `IDirect3DDevice9*`.
- Adapter offset `0x40`: `IDirect3D9*`.

`FUN_00451110` calls `IDirect3D9::CreateDevice` through vtable offset `0x40`,
then initializes render resources and default projection/state.

### Main Render Loop

`FUN_00421530` is the main per-render-entry loop. For each active entry it:

1. applies target/viewport state;
2. calls `FUN_0044e580` to set projection;
3. calls `FUN_0044e400` to set view only when the camera matrix differs from the
   last submitted matrix;
4. flushes render command lists through `FUN_004342c0`.

This conditional view submission is a likely reason Remix can miss valid camera
state on a later render-target pass. A clean hook may need to preserve and resubmit
the last valid view/projection before real world draw lists.

### Camera And World Transforms

Instruction search for `IDirect3DDevice9::SetTransform` vtable offset `0xb0`
shows the relevant calls:

- `FUN_0044def0`: `SetTransform(D3DTS_WORLD = 0x100, matrix)`.
- `FUN_0044e400`: builds and submits `D3DTS_VIEW = 2`.
- `FUN_0044e580`: builds and submits `D3DTS_PROJECTION = 3`.
- `FUN_0044d2e0`, `FUN_0044d5d0`, `FUN_0044ee70`: texture transforms.

The world/view/projection path is fixed-function D3D9 state, not shader-only.

### Static World Geometry

`FUN_00884eb0` is the Model render/renderlist emission method. It does not call
D3D directly; it emits draw opcodes.

Default static model path:

```text
FUN_00884eb0
  -> FUN_00432c70     ; emits opcode 0x11
  -> FUN_004342c0     ; renderlist interpreter
  -> FUN_004540e0     ; SetStreamSource + SetFVF + SetIndices + DrawIndexedPrimitive
```

Alternate/special path:

```text
FUN_00884eb0
  -> FUN_00432cc0     ; emits opcode 0x12
  -> FUN_004342c0
  -> FUN_00454660     ; two streams + SetVertexDeclaration + DrawIndexedPrimitive
```

Generic FVF path:

```text
FUN_00884980
  -> FUN_00432d70     ; emits opcode 0x15
  -> FUN_004342c0
  -> FUN_004507b0 / FUN_0044fc40
```

Conclusion: TOD does submit normal indexed 3D geometry. RTX failure is not caused
by absence of D3D9 world geometry.

### Render Target Switching

Instruction search shows `SetRenderTarget` (`+0x94`) and
`SetDepthStencilSurface` (`+0x9c`) are centralized in `FUN_0044e220`.

`FUN_0044e220(param_2)`:

- when `param_2 != 0`, binds a texture surface as RT0 and pairs depth;
- when `param_2 == 0`, restores the backbuffer/depth surface;
- updates adapter width/height and viewport.

This matches runtime evidence that mission 3 depends on Remix's native:

```text
rtx.raytracedRenderTarget.enable = True
rtx.raytracedRenderTargetTextures = ...
```

The ASI render-target redirect and Remix native raytraced-render-target config are
not substitutes. They must coexist, as proven by the known-good build.

### Texture Binding

Instruction search shows engine `SetTexture` (`+0x104`) is centralized:

- `FUN_0044f8a0`: flushes dirty texture stages.
- `FUN_004634b0`: binds the wrapper's D3D texture.

The engine appears to use two tracked stages in this path. The known-good ASI's
`Hook_SetTexture` substitution at bind time, for every stage, matches this model.
The later stage-0 draw-time substitution rewrite is not aligned with the engine.

Runtime texture reads/hash computation are not part of the proper support path.
They are diagnostic tooling only and have already proven unsafe for mission 3.

## Clean Support Boundary

The ASI should have only these runtime responsibilities:

1. Device/vtable plumbing.
2. Preserve the latest valid fixed-function view/projection.
3. Resubmit view/projection immediately before real non-RHW world draws or before
   world renderlist flushes.
4. Keep the known-good managed texture DEFAULT-copy behavior at `SetTexture`, with
   no hash/readback/discovery reads beyond that generic copy path and no draw-time
   stage rewrite.
5. Preserve the known-good render-target behavior and config:
   `rtx.raytracedRenderTarget.enable` plus the current RT hash list.

Content decisions belong in Remix config:

- UI/HUD texture hashes: `rtx.uiTextures`.
- Broken/undesired world instances: `rtx.hideInstanceTextures`.
- Scene RT descriptor hashes: `rtx.raytracedRenderTargetTextures`.

## Recommended Next Step

Do not attempt HUD fixes in the ASI yet. First solve the mission-3 angle-dependent
RTX dropout from the protected baseline.

The Ghidra-backed next investigation should be:

1. Run the protected baseline and reproduce the angle where RTX drops.
2. Inspect only render-target/camera evidence:
   - active requested RT in `Hook_SetRenderTarget`;
   - whether that RT is one of the configured `raytracedRenderTargetTextures`;
   - last non-identity view/projection before `DrawIndexedPrimitive`;
   - whether the failing angle switches to another render entry/RT.
3. If a new RT descriptor hash is missing, add it to
   `rtx.raytracedRenderTargetTextures` in config.
4. If camera state is missing only for a known world renderlist pass, prefer an
   engine-aware hook around `FUN_004342c0`/`FUN_004540e0` that resubmits cached
   view/projection before opcode `0x11`/`0x12`/`0x15` draws.

No broad UI shadowing, diagnostic texture dumping, dirty refresh machinery, or
mission-specific code in the ASI. If UI support is needed at the D3D9 boundary,
the only acceptable content discriminator is Remix config (`rtx.uiTextures`), not
texture dimensions or game-specific C++ hash lists.

## Clean Runtime Build - 2026-06-02

Built a clean runtime ASI from the protected known-good source, preserving the
same proven support behavior while compiling out runtime diagnostics.

Live binary:

```text
scripts\TODCameraResend.asi
size: 119296 bytes
timestamp: 2026-06-02 19:42:49
```

Protected fallback:

```text
tools\TODCameraResend.known_good_before_clean_20260602.asi
size: 132608 bytes
```

Source changes:

- Added `TOD_ENABLE_RUNTIME_LOG = 0`.
- Added a source-level runtime support boundary comment tied to the Ghidra facts.
- Compiled logging calls out with the preprocessor; no runtime log arguments,
  formatting, file I/O, candidate dumps, or texture hash/readback discovery is
  active.
- Preserved the known-good behavior:
  - fixed-function view/projection cache and resend before non-RHW indexed draws;
  - generic texture-backed render-target redirect plus stale RT-composite skip;
  - known-good managed texture `SYSTEMMEM -> DEFAULT` copy at `SetTexture` time;
  - no draw-time texture rewrite and no stage-0-only substitution.

Additional Ghidra checks:

- `FUN_0044e400` has one caller (`0042195e`) inside the main render loop.
- `FUN_0044e580` has extra setup/renderlist callers, so hooking only one engine
  caller would miss valid projection updates.
- All `SetRenderTarget` calls remain centralized in `FUN_0044e220`.
- `FUN_00434290` flushes the three renderlists for an object by calling
  `FUN_004342c0(param + 0x2c)`, `FUN_004342c0(param + 0x18)`, and
  `FUN_004342c0(param + 0x40)`.

Conclusion: the cleanest stable runtime boundary is still D3D9 vtable state
tracking/resend plus TOD-specific-but-generic RT/composite handling. Inline hooks
around a single engine caller are not cleaner right now because projection has
multiple valid engine paths and renderlists are flushed from several places.

Next runtime check: launch mission 3 from this clean build. If the old
angle-dependent RTX dropout still appears, investigate only camera/RT state. Do
not reintroduce HUD refresh/dirty tracking into the ASI.

## Config-Owned HUD Support - 2026-06-02

Additional Ghidra check:

- `place_in_hud` is a model flag stored at object offset `0x6c`, bit `0x8000`.
- Setter label `00883dc0` writes the bit; getter label `00883df0` reads it.
- The current ASI is intentionally a D3D9 vtable shim and does not receive model
  object pointers. A direct `place_in_hud` solution would require inline hooks in
  TOD's model/renderlist emission path, which is a heavier support boundary than
  the confirmed HUD hash config requires.

Updated clean boundary:

- Preserve the known-good `SetTexture`-time managed-texture substitution model
  for every stage. This matches `FUN_0044f8a0 -> FUN_004634b0`, where TOD flushes
  its tracked texture stages.
- Keep generic compressed managed texture copying.
- Remove the uncompressed A8 dimension whitelist as a shipping runtime decision.
- For uncompressed managed textures, compute the RTX hash once at first bind and
  create a DEFAULT-pool copy only if that hash is explicitly present in
  `rtx.uiTextures`.
- Do not hook texture LockRect/UnlockRect/AddDirtyRect, do not refresh default
  copies on bind, and do not hardcode HUD hashes in C++.

Live build:

```text
scripts\TODCameraResend.asi
size: 136704 bytes
timestamp: 2026-06-02 20:11:13
```

Rationale:

- The previous 151 KB lean UI attempt failed because it combined UI hash matching
  with dirty invalidation and runtime refresh. This build ports only the clean
  config-owned matching piece.
- Content still belongs in config: the 22 confirmed HUD hashes remain in
  `rtx.uiTextures`; the ASI only makes those configured managed textures visible
  to Remix as hashable DEFAULT-pool textures.

Rejection after test:

- User reported this build regressed to "RTX gone / screen glitch back".
- The build was rolled back to the clean runtime ASI:

```text
scripts\TODCameraResend.asi
size: 119296 bytes
timestamp: 2026-06-02 19:42:49
```

- Active source was restored to the known-good source backup with no `XXH`,
  runtime `rtx.uiTextures` parser, configured-UI hash matching, diagnostic
  logger, or dirty/refresh hooks.

Updated rule:

- Do not compute RTX hashes from arbitrary managed textures inside the shipping
  ASI, even once. TOD's managed A8 path is too sensitive.
- A proper HUD fix must be engine-aware. Continue Ghidra analysis around
  `place_in_hud`, model renderlist emission, and the texture wrapper/state path
  before adding any runtime behavior.

## Reversible D3D-Semantic HUD Test - 2026-06-02

The next live test is intentionally not a return to runtime texture-content
probing. It is a narrow D3D-semantic gate derived from the already captured HUD
draw taxonomy:

- keep the known-good SetTexture-time managed-copy behavior for mission/world
  rendering;
- mark `worldDrawnThisFrame` only from non-pretransformed draws to the primary
  render target;
- after that mark, temporarily bind a DEFAULT-pool copy only for pretransformed
  RHW/POSITIONT draws on the primary target whose stage-0 texture is a managed
  regular `A8R8G8B8`;
- restore the original stage-0 texture immediately after the draw;
- compile runtime logging out (`TOD_ENABLE_RUNTIME_LOG 0`);
- do not parse `rtx.uiTextures`, compute RTX hashes, hook LockRect/UnlockRect,
  refresh dirty textures, or dump diagnostic candidates.
- leave the protected baseline's old one-mip A8 whitelist unchanged for this
  test, but do not expand it or treat dimension matching as the HUD strategy.

This is still a runtime-support experiment, not a final proof that TOD's
`place_in_hud` engine flag has been wired through. If it regresses mission RTX,
reject it and return to the protected 119296-byte rollback ASI before continuing
with deeper engine-side `place_in_hud`/renderlist analysis.
