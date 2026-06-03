# TOD Render Distance And Texture Handling RE Notes

Date: 2026-06-03

Scope: Ghidra-backed notes for fixing RTX Remix support cleanly by understanding
`TOD.exe` camera, frustum/culling, renderlist, and material/texture paths.

## Current Preserved State

The current working runtime state was copied to:

```text
C:\GOG Games\TotalOverDoseRTXRemix\state4
```

State4 currently represents:

- RTX active.
- HUD visible.
- Sparks/blood visible.
- Previously missing breakable/corrugated panel texture visible.
- Opaque managed-A8 world texture copying removed after sky/world material
  corruption.
- Sky may be black; proper sky/backdrop classification is deferred.

Important active files preserved in state4:

- `scripts\TODCameraResend.asi`
- `scripts\rtx.conf`
- `scripts\dxvk.conf`
- `tools\tod_camera_resend_asi\TODCameraResend.cpp`
- `re_docs\*.md`

## Render Distance / Capture Framing

### Projection Setup

`FUN_0044e580 @ 0044e580` builds and submits the D3D projection matrix.

Relevant decompile facts:

- It calls `FUN_009676b4`.
- It submits `D3DTS_PROJECTION = 3` through `IDirect3DDevice9::SetTransform`
  (`vtable + 0xb0`).
- The projection builder receives a near/far pair:

```c
FUN_009676b4(param_1 + 0x1559, fov, aspect, near_clip, far_clip);
(**(code **)(*(int *)*param_1 + 0xb0))((int *)*param_1, 3, param_1 + 0x1559);
```

`FUN_009676b4 @ 009676b4` builds a standard D3D-style perspective projection:

```c
param_5 = param_5 / (param_5 - param_4);
param_1[10] = param_5;
param_1[0xb] = 1.0;
param_1[0xe] = -(param_5 * param_4);
```

Inference:

- `param_4` is near clip.
- `param_5` is far clip.
- Increasing render distance properly means changing the engine far clip before
  projection/frustum construction, not changing Remix texture settings.

### Main Render Loop

`FUN_00421530 @ 00421530` is the main render-entry loop.

For each active render entry it:

- applies target/viewport state;
- calls `FUN_0044e580` using render-entry offsets `+0xe0`, `+0xe4`, `+0xe8`,
  `+0xec`;
- conditionally calls `FUN_0044e400` to submit view only if the camera matrix
  differs from the last submitted one;
- flushes render command lists through `FUN_004342c0`.

This is why the ASI's camera resend is a valid support layer: TOD can skip
submitting view state on later passes when it believes the view is unchanged.

### Frustum Construction And Culling

`FUN_004086a0 @ 004086a0` is the compact frustum/sphere visibility test used by
draw submitters. It tests a transformed sphere against five frustum planes and
returns `0` for invisible.

Confirmed callers include:

```text
00425286
0042CA12
0042CD17
0042CD32
0045432F  ; inside FUN_004540e0
00454899  ; inside FUN_00454660
008A0BCE
008DDEE4
008DDFA5
008DE00B
008DE026
```

`FUN_004540e0` and `FUN_00454660` both call `FUN_004086a0` immediately before
real indexed draws. If it returns invisible, they return before calling
`DrawIndexedPrimitive`.

Conclusion:

- RTX Remix cannot capture/draw world geometry that TOD has already culled.
- If Remix capture cannot get the whole visible/expected scene, the cause may be
  engine frustum/LOD/culling before D3D submission.
- A proper fix should target camera far clip / LOD / culling at the engine
  boundary, not force broad texture-copy behavior in the ASI.

### Far/Near/LOD Script Properties

Raw string scan found engine/editor properties that directly match render
distance and LOD:

```text
farclip                string @ 009CC9D8, reference @ 0087E319
nearclip               string @ 009CCA00, reference @ 0087E2CA
set_lod_factor         string @ 009BC88C, reference @ 0048786E
forcelodcalculation    string @ 009CD758, reference @ 00890F07
lod_distance           string @ 009CE0D8, reference @ 00890359
traverse_distance      string @ 009CE100, reference @ 0089023D
lod_threshold          string @ 009CE158, reference @ 00890158
```

`FUN_0087de40` references camera far/near values and calls the frustum builder.
It uses camera-object offsets around:

```text
camera + 0xb4
camera + 0xb8
camera + 0xbc
```

The `farclip` string reference at `0087E319` strongly suggests `camera + 0xbc`
is the far clip property.

Default frustum setup:

```text
FUN_004087c0 -> FUN_00406e60(..., 0x428C0000, 0x42480000, 0x447A0000)
```

Notable constants:

```text
0x428C0000 = 70.0f
0x42480000 = 50.0f
0x447A0000 = 1000.0f
```

Inference:

- TOD has a default far/cull distance of about `1000.0f` in at least one
  frustum path.
- The active game camera likely has script-controlled `nearclip` / `farclip`.
- Clean render-distance work should first locate and adjust that camera farclip
  path, or expose/patch it generically, before considering any culling bypass.

## D3D Renderlist And Texture Binding

### Renderlist Interpreter

`FUN_004342c0 @ 004342c0` interprets render opcodes.

Important draw opcodes:

```text
0x11 -> FUN_004540e0  ; static/default indexed mesh path
0x12 -> FUN_00454660  ; alternate/two-stream vertex-declaration indexed path
0x15 -> FUN_004507b0  ; generic FVF path, later draws through FUN_0044fc40
```

### Static Mesh Submit Path

`FUN_004540e0 @ 004540e0`:

- performs frustum visibility via `FUN_004086a0`;
- sets stream source;
- sets FVF;
- sets indices;
- flushes dirty texture stages through `FUN_0044f8a0`;
- calls `IDirect3DDevice9::DrawIndexedPrimitive` (`vtable + 0x148`).

### Alternate / Two-Stream Submit Path

`FUN_00454660 @ 00454660`:

- performs the same frustum visibility style through `FUN_004086a0`;
- uses two streams;
- sets a vertex declaration;
- flushes textures through `FUN_0044f8a0`;
- calls `DrawIndexedPrimitive`.

This path also temporarily changes render states around the draw. It is one
reason stage-0-only draw-time texture substitution is not aligned with the
engine. TOD can use at least two texture stages.

### Texture Flush / Bind

`FUN_0044f8a0 @ 0044f8a0` flushes dirty texture stages.

Important facts:

- It loops over exactly two stages.
- Dirty flags live near adapter offset `0x9708`.
- Bound engine texture wrappers live near adapter offset `0x96f8`.
- If the stage wrapper is null, it calls `SetTexture(stage, null)`.
- Otherwise it calls `FUN_004634b0`.

`FUN_004634b0 @ 004634b0` performs the real D3D bind:

```c
(**(code **)(*(int *)*DAT_00a39f14 + 0x104))
  ((int *)*DAT_00a39f14, stage, *(uint *)(texture_wrapper + 4) & 0xfffffffc);
```

Conclusion:

- TOD's clean texture boundary is the centralized `SetTexture` flush/bind path,
  not arbitrary draw-time stage-0 substitution.
- If the ASI needs managed-texture DEFAULT copies, doing it at `SetTexture` time
  for the engine-bound texture stages matches the engine model better than
  broad draw-time rewriting.

## Model / Material Properties

Model registration remains anchored at:

```text
FUN_00889800 @ 00889800
```

Relevant model/editor properties:

```text
active_texture_set
number_of_textures_sets
opacity
addblend
disablezwrite
use_hard_alpha_factor
hard_alpha_factor
place_in_hud
dynamically_lit
statically_lit
backside_transparent
single_color_mode
```

Useful decompiled accessors:

```text
FUN_00884140  active_texture_set getter -> *(char *)(model + 100)
FUN_00884150  active_texture_set setter -> writes model + 100
FUN_008840B0  dynamically_lit setter -> model + 0x6c bit 0x200000
FUN_00884100  statically_lit getter -> model + 0x6c bit 0x400000
FUN_00883F90  opacity getter -> low byte of model + 0x6c scaled by 1/255
```

Known string references for material/HUD properties:

```text
place_in_hud string @ 009CD010, reference @ 00889BF2
```

Interpretation:

- TOD already knows whether content is HUD, alpha blended, alpha tested,
  z-write-disabled, dynamically lit, statically lit, etc.
- Future fixes should use engine/material/render-state categories where possible
  instead of texture dimensions or per-texture C++ hash lists.

## Clean Fix Direction

Render distance / capture framing:

1. Continue from the current state4.
2. Decompile the functions around:

```text
0087E2CA  nearclip registration/reference
0087E319  farclip registration/reference
00890F07  forcelodcalculation registration/reference
00890359  lod_distance registration/reference
0089023D  traverse_distance registration/reference
00890158  lod_threshold registration/reference
0048786E  set_lod_factor registration/reference
```

3. Prefer an engine-level farclip/LOD adjustment if one can be set cleanly.
4. Only consider a `FUN_004086a0` culling bypass as a diagnostic build, not as a
   shipping fix, because bypassing engine culling can draw too much, break LOD
   behavior, and hurt performance.

Texture/material handling:

1. Keep runtime logging and candidate dumping out of the shipping ASI.
2. Keep the current narrow render-state A8 categories that fixed HUD,
   sparks/blood, and the breakable panel.
3. Do not restore the broad opaque managed-A8 world copy category; it correlated
   with sky/world material corruption.
4. If another invisible texture class appears, identify its TOD material flags
   or renderlist state first. Avoid hashes/sizes in C++ unless used only in
   external Remix config (`rtx.uiTextures`, `rtx.skyBoxTextures`,
   `rtx.hideInstanceTextures`).

