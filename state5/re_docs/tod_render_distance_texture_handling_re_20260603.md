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

## Implemented Farclip Test Build - 2026-06-03 00:31 +05:30

The ASI now contains a guarded engine-camera farclip override for testing render
distance / Remix capture framing:

```text
TOD.exe camera-system global:
  VA  00A3DCBC
  RVA 0063DCBC

Camera-system slots:
  +0x60 primary/requested camera
  +0x64 secondary/requested camera
  +0x6C current camera

Camera clip fields:
  camera + 0xB8 nearclip
  camera + 0xBC farclip

Test override:
  farclip = 3000.0f
```

The patch runs from `Hook_Present()` once per presented frame. It reads the
current camera-system pointer from the main executable module base plus the
RVA above, checks each camera slot, validates memory with `VirtualQuery`, and
only writes `camera + 0xBC` when:

- the camera pointer is committed writable memory;
- `nearclip` and `farclip` are finite;
- `nearclip` is in a normal camera range (`0.01..10`);
- `farclip` is in a sane TOD range (`10..10000`) and greater than nearclip;
- current `farclip` is below the test value.

This is an engine-property patch, not a draw-call or texture workaround. It uses
the same camera field registered by TOD's `farclip` script/editor property and
should affect both projection and engine frustum submission after the first
presented frame.

Build result:

```text
Live ASI:
  scripts\TODCameraResend.asi
  size 121856
  timestamp 2026-06-03 00:31:02

Saved test ASI:
  tools\TODCameraResend.camera_farclip_3000_20260603.asi

Rollback ASI:
  tools\TODCameraResend.pre_farclip_20260603.asi

Saved test source:
  tools\tod_camera_resend_asi\TODCameraResend.camera_farclip_3000_20260603.cpp.bak

Rollback source:
  tools\tod_camera_resend_asi\TODCameraResend.pre_farclip_20260603.cpp.bak
```

State4 remains the preserved working baseline and has not been overwritten by
this farclip test build.

## SkyBox Geometry Config Test - 2026-06-03 01:10 +05:30

User result after the farclip build:

- More world geometry appears to reach Remix, so the camera farclip patch is
  likely working.
- Next visible class is sky/backdrop handling.

RE / capture basis:

- TOD registers an explicit `SkyBox` class at `FUN_008f22a0`.
- `FUN_008f20b0` builds/loads skybox geometry resources into globals near
  `DAT_00a3e0b0`.
- Existing sky/backdrop screenshots and captures show this is not an ASI texture
  copy problem. It is a Remix sky classification problem.
- A prior sky capture contains material/texture hash `86CB3E6CC84F9371`, a
  solid blue/teal sky-like texture. The meshes using that material were:

```text
mesh_08AB62B56C25BD67
mesh_0B600FFAE9C09AD0
mesh_734739A160FD48E6
```

Config change applied to both `scripts\rtx.conf` and root `rtx.conf`:

```ini
rtx.skyBoxGeometries = 0x08AB62B56C25BD67, 0x0B600FFAE9C09AD0, 0x734739A160FD48E6
```

Notes:

- `rtx.skyAutoDetect = 2` was left enabled for this pass.
- Geometry tagging is preferred over texture tagging because TOD can reuse
  terrain/backdrop textures in ordinary world surfaces.
- This is config-only. No ASI code was changed.
- Backups:

```text
tools\scripts_rtx.pre_skybox_geometry_20260603.conf.bak
tools\rtx.pre_skybox_geometry_20260603.conf.bak
```

Follow-up after mission 2 stayed black:

- The bridge log did not reject `rtx.skyBoxGeometries`.
- Local `.trex\d3d9.dll` contains `rtx.skyBoxTextures` and
  `rtx.skyDrawcallIdThreshold`.
- No fresh capture was created at the current black-sky mission-2 angle, so the
  live mission-2 sky hashes are still unknown.

Additional config test applied:

```ini
rtx.skyBoxTextures = 0x86CB3E6CC84F9371
rtx.skyDrawcallIdThreshold = 12
```

Backups:

```text
tools\scripts_rtx.pre_sky_drawcall_texture_20260603.conf.bak
tools\rtx.pre_sky_drawcall_texture_20260603.conf.bak
```

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

## Sky RE / Remix Config Update - 2026-06-03

Ghidra facts:

- `FUN_008f22a0` registers the TOD `"SkyBox"` class.
- `FUN_008f2360` is the SkyBox constructor and sets engine/model flag
  `0x08000000` on the object/model field at `param_1[0xc] + 0x1c`.
- `FUN_008f20b0` builds/generated static sky mesh resources into globals
  beginning at `DAT_00a3e0b0`.
- Direct D3D sky draw hooks were not found in the SkyBox class itself; the
  object appears to feed generic render-list/model submission.

Current clean config hypothesis:

- TOD sky geometry can pass through normal depth-tested render-list paths.
- Remix `rtx.skyAutoDetect = 2` is therefore too strict because the local Remix
  option text says mode 2 only treats the first camera as sky when depth testing
  is disabled.
- Manual `rtx.skyBoxGeometries` from an old capture was removed because the
  latest capture shows different sky-like mesh hashes and stale geometry tags
  can explain mission-specific distorted sky/backdrop geometry.

Applied config-only test:

```ini
rtx.skyAutoDetect = 1
```

Removed:

```ini
rtx.skyBoxGeometries = 0x08AB62B56C25BD67, 0x0B600FFAE9C09AD0, 0x734739A160FD48E6
```

No ASI code was changed for this sky pass.

## Mission-2 Orange Sky Probe Follow-up - 2026-06-03

After switching to `rtx.skyAutoDetect = 1`, mission 1 filled its sky, but
mission 2/3 got a yellow/orange sky.

Capture `capture_2026-06-03_02-04-52.usd` showed the sky probe was polluted by
a sun/lens-flare sprite:

```text
sky_3EC6766F06C8CB82
mesh_3EC6766F06C8CB82 -> mat_5EF9EBC260F4B6BC
```

Texture `5EF9EBC260F4B6BC` is a small yellow flare sprite in the texture sheet,
not a skybox. This confirms that mode 1 can over-classify TOD flare/effect
draws as sky.

Config-only mitigation now being tested:

```ini
rtx.skyAutoDetect = 1
rtx.ignoreTextures = 0x5EF9EBC260F4B6BC
```

This keeps the broader sky detection that helped mission 1, while excluding the
confirmed false-positive flare from Remix processing. No ASI changes.

## Explicit Sky Geometry Follow-up - 2026-06-03

The flare-ignore test made all three mission skies black. The new captures had
no `sky_*` instances and no `T_SkyProbe.dds`, which means broad mode-1
autodetection had only been accepting the flare-like false positive in those
views.

The city/mission-3 capture still includes a real sky-material family:

```text
mat_86CB3E6CC84F9371 / texture 86CB3E6CC84F9371
```

Confirmed geometry bound to that material:

```text
mesh_08AB62B56C25BD67
mesh_0B600FFAE9C09AD0
mesh_734739A160FD48E6
mesh_97B9E3F5DBA2BD1B
mesh_B67B874D7BCDD65D
```

Current config-only test:

```ini
rtx.skyAutoDetect = 0
rtx.skyBoxGeometries = 0x08AB62B56C25BD67, 0x0B600FFAE9C09AD0, 0x734739A160FD48E6, 0x97B9E3F5DBA2BD1B, 0xB67B874D7BCDD65D
```

`rtx.ignoreTextures = 0x5EF9EBC260F4B6BC` was removed because explicit sky
geometry avoids the flare auto-detect path without hiding the flare globally.

## TOD SkyBox Engine Path - 2026-06-03

Config-only sky tagging is not stable across missions. The explicit geometry
hashes came from one captured sky material, but user testing still produced
black sky / sun-only frames in mission 1 and colored ray artifacts in mission 3.

Ghidra recovered the actual engine-side sky path:

```text
SkyBox class string: 009D2EE4 "SkyBox"
SkyBox class registration: FUN_008F22A0
SkyBox constructor: FUN_008F2360
SkyBox update/submit method: TOD_SkyBox_UpdateOrSubmit_008F1E10
Generated static sky mesh globals: DAT_00A3E0B0[0..4]
Render-list mesh opcode writer: FUN_00432C70
Render-list interpreter: FUN_004342C0
Indexed mesh draw routine: FUN_004540E0
```

Important behavior:

```text
FUN_008F2360 sets engine/model flag 0x08000000 on the SkyBox render object.
TOD_SkyBox_UpdateOrSubmit_008F1E10 creates/reuses a private render list at +0x7C.
When the sky list is dirty, it emits five mesh draws:
  for i in 0..4:
    FUN_00856EC0(...)
    FUN_00431660(...)
    FUN_00432C70(DAT_00A3E0B0[i])
It disables depth and depth write through render-list opcodes before those draws.
It submits the list through FUN_00436040(0, 0) and FUN_0041FD90(layer=2, value=0).
```

`FUN_004540E0` shows the stable D3D buffer relation for opcode `0x11` mesh
draws:

```text
mesh + 0x0C -> vertex-buffer wrapper pointer, low 2 bits are flags
mesh + 0x10 -> index-buffer wrapper pointer, low 2 bits are flags
vb wrapper + 0x24 -> IDirect3DVertexBuffer9*
ib wrapper + 0x1C -> IDirect3DIndexBuffer9*
```

Clean implementation chosen:

- Do not maintain mission-specific `rtx.skyBoxGeometries`.
- During each indexed draw, query the current D3D stream-0 vertex buffer and
  index buffer.
- Compare them to the five TOD SkyBox mesh buffers from `DAT_00A3E0B0[0..4]`.
- If matched, temporarily set viewport `MinZ=0.999, MaxZ=1.0` only around that
  draw, then restore the original viewport.
- Use Remix `rtx.skyAutoDetect = 2` plus `rtx.skyMinZThreshold = 0.99` so Remix
  sees a generic D3D sky cue instead of per-mission hashes.

This is an engine-aware D3D bridge, not a content hash patch. It should apply
wherever TOD uses the `SkyBox` class and its generated sky mesh list.

## Sky Asset and Runtime Texture Discovery - 2026-06-03

User testing showed the viewport-marker bridge is only partial: mission 1/2
rendered a blue patch at the upper-left while most of the sky remained black,
and mission 3 did not get a stable gameplay sky. That result means the SkyBox
mesh path is correct, but the Remix-side classification cue is insufficient.

Static archive strings in `blocks.naz` confirm TOD has real skybox asset
families, including:

```text
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox.model
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_FR.bmp
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_LF.bmp
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_BK.bmp
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_RT.bmp
/data/Textures/Skybox/Skybox_DAWN/Skybox_DAWN/skybox_UP.bmp
/data/Textures/Skybox/RedDustSky/RedDustSky/skybox.model
/data/Textures/Skybox/RedDustSky/RedDustSky/skybox_FR.bmp
```

The same archive contains flare assets that previously polluted Remix sky
autodetect:

```text
/Textures/Sprites/sun_flare.bmp
/Textures/Sprites/sun_flare_star.bmp
/Textures/Sprites/Sun_OverBurn.bmp
/Textures/Sprites/Flare01.bmp
/Textures/Sprites/Flare02.bmp
```

Clean next step:

- Do not add mission-specific C++ sky cases.
- Disable the partial viewport marker.
- Add a compile-time-gated SkyBox texture discovery path.
- Match only the five engine SkyBox meshes from `DAT_00A3E0B0[0..4]`.
- For those draws only, hash and dump the original stage-0 TOD source texture.

Diagnostic build state:

```text
kMarkTodSkyDrawsWithViewportMinZ = false
kEnableTodSkyTextureDiscovery = true
rtx.skyAutoDetect = 0
outputs:
  rtx-remix\logs\tod-sky-textures.tsv
  rtx-remix\logs\tod-sky-textures\skybox_<hash>_<WxH>_<format>.dds
scripts\TODCameraResend.asi size: 131584 bytes
timestamp: 2026-06-03 11:23:37
```

After one short run through missions 1, 2, and 3, inspect the dumped DDS files
and set Remix sky configuration from confirmed SkyBox texture hashes only. Then
turn `kEnableTodSkyTextureDiscovery` off and rebuild so the ASI returns to a
lean runtime bridge.

Black-screen correction:

The `rtx.skyAutoDetect = 0` diagnostic made the whole game black before any
SkyBox texture log was produced. Active state is now:

```text
kMarkTodSkyDrawsWithViewportMinZ = false
kEnableTodSkyTextureDiscovery = true
rtx.skyAutoDetect = 2
rtx.skyMinZThreshold = 0.99
scripts\TODCameraResend.asi size: 132096 bytes
timestamp: 2026-06-03 11:59:26
```

The SkyBox draw hook now only queues the stage-0 SkyBox source texture. Hashing
and DDS dumping run at `Present`, outside the draw call, to avoid locking TOD
texture memory while the engine is submitting geometry.

## Forced TextureMap Probe - 2026-06-03

The cleaner way to avoid manual mission touring is to force already-loaded TOD
textures through the engine's own texture debug path instead of drawing fake
geometry from the ASI.

`TOD_tools` identifies:

```text
Texture::DrawAllTextures @ 0x00463850
Texture::TexturesMap     @ 0x00A39F50
```

`Texture::DrawAllTextures()` iterates `Texture::TexturesMap` and asks the engine
to render a tiny textured quad for each currently loaded texture. The ASI now
calls this function for five `Present` frames while a compile-time diagnostic
flag is enabled:

```text
kEnableTodTextureMapProbe = true
kTodTextureDrawAllTexturesVa = 0x00463850
kTodTextureMapProbeFrames = 5
```

During those forced frames, stage-0 `SetTexture` calls are queued and processed
at `Present` using the same RTX hash / DDS dumping path as the SkyBox-only
probe. Outputs:

```text
rtx-remix\logs\tod-forced-texture-probe.tsv
rtx-remix\logs\tod-forced-texture-probe\texture_<hash>_<WxH>_<format>.dds
```

This is a diagnostic loader, not a permanent runtime feature:

- It only renders textures TOD has already loaded.
- It does not load mission-specific archives by name.
- It does not put sky hashes or mission names in C++.
- If it exposes the skybox DDS files, final sky classification should still be
  done in Remix config, then `kEnableTodTextureMapProbe` should be set to
  `false` and the ASI rebuilt.

Build:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 134144 bytes, timestamp 2026-06-03 12:20:44
```

Result of one boot/menu run:

- `tod-forced-texture-probe.tsv` was produced.
- Only 23 textures were captured.
- Contact sheet `re_docs\forced_texture_probe_contact_sheet.png` shows UI/HUD,
  flag/icon, video, and render-target textures.
- No SkyBox family textures were present.

Conclusion: `Texture::DrawAllTextures()` works, but the boot/menu state has not
loaded the SkyBox texture families into `Texture::TexturesMap`. The next clean
step is to force TOD's own texture asset loader before calling
`DrawAllTextures()`.

## Forced Sky Texture Asset Load Probe - 2026-06-03

Ghidra decompile of TOD's loadscreen texture path (`FUN_0087C2F0`) gives a
known-good native texture load pattern:

```text
DAT_00A3BE18 = 6
DAT_00A3BE2D = 1
FUN_00878AB0(resourcePath)   ; LoadNativeResource
DAT_00A3BE2D = 0
DAT_00A3BE18 = 0
```

Ghidra decompile of `AssetLoader::LoadAssetByName @ 0x008FFC10` showed it is
not suitable as the ASI entry point because it calls
`IncreaseResourceReferenceCount(*asset)` with no null guard after
`LoadNativeResource`. The ASI therefore calls `LoadNativeResource` directly and
logs null/exception results instead of blindly refcounting.

Added a second compile-time-gated diagnostic:

```text
kEnableTodSkyAssetLoadProbe = true
kTodLoadNativeResourceVa = 0x00878AB0
kTodTextureAssetAllocatorGlobalVa = 0x00A3BE18
kTodTextureAssetLoadFlagGlobalVa = 0x00A3BE2D
```

At first `Present`, the ASI temporarily applies the same two globals TOD uses
for loadscreen texture loading, calls `LoadNativeResource` for each known
SkyBox BMP path found in `blocks.naz`, restores the globals, then lets the
existing `Texture::DrawAllTextures()` probe render/hash/dump the loaded
textures.

Loader output:

```text
rtx-remix\logs\tod-sky-asset-load-probe.tsv
```

Texture output remains:

```text
rtx-remix\logs\tod-forced-texture-probe.tsv
rtx-remix\logs\tod-forced-texture-probe\texture_<hash>_<WxH>_<format>.dds
```

This is still diagnostic-only. If this exposes the sky faces, final behavior
belongs in Remix config and both `kEnableTodSkyAssetLoadProbe` and
`kEnableTodTextureMapProbe` should be disabled for the shipping ASI.

Build:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 137216 bytes, timestamp 2026-06-03 12:53:40
```

Runtime result:

The game crashed on startup with this probe enabled. The loader log shows 13
successful resource loads repeated across three launch attempts, then the
process died before the next path could be logged:

```text
loaded /data/Textures/Skybox/ClearBlueSky/ClearBlueSky/skybox_BK.bmp
...
loaded /data/Textures/Skybox/Sky/cloudSky/skybox_LF.bmp
next unlogged path: /data/Textures/Skybox/Sky/cloudSky/skybox_RT.bmp
```

No updated forced texture-map rows were produced after the loader rows, so the
crash happened before the loaded assets could be rendered/hashed.

Conclusion:

Direct `LoadNativeResource` from `Present` is not a safe generic sky preload
mechanism. It can return non-null asset pointers but still corrupt the runtime
resource/block ownership state because SkyBox assets are normally selected and
loaded under scene/block context.

Rollback build:

```text
kEnableTodSkyAssetLoadProbe = false
kEnableTodTextureMapProbe = false
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 135168 bytes, timestamp 2026-06-03 18:43:49
```

Clean remaining approach:

- Keep startup asset forcing disabled.
- Use the existing SkyBox draw/material path after TOD naturally loads the
  current scene's sky family.
- If broader coverage is needed, hook/log the SkyBox material bind
  (`FUN_00856EC0` / `FUN_00431660`) rather than preloading unrelated assets at
  startup.

## SkyBox Draw Texture Capture Fallback - 2026-06-03

Ghidra decompile of the real SkyBox submission loop:

```text
SkyBox::Render @ 0x008F1E10
  ECX = SkyBox->ModelRes
  FUN_00856EC0(0, faceIndex, 0)  ; returns material pointer
  FUN_00431660(renderList, material, 0)
  FUN_00432C70(DAT_00A3E0B0[face])
```

The render-list interpreter handles opcode `1` by calling:

```text
FUN_0044EF70(material, stage)
  marks DAT_00A39F14 + 0x9708 + stage dirty
  stores material at DAT_00A39F14 + 0x96F8 + stage * 4

FUN_0044F8A0()
  flushes dirty texture stages
  FUN_004634B0(material, stage)
    IDirect3DDevice9::SetTexture(stage, *(material + 4) & ~3)
```

The ASI already matches real SkyBox draws by comparing the current D3D VB/IB
against `DAT_00A3E0B0[0..4]`. To make that capture more robust, the queued
SkyBox texture processor now hashes:

1. The original source texture captured by the ASI's `SetTexture` hook.
2. If that source is missing from the ASI registry, the bound DEFAULT copy.

Rows using the fallback are marked:

```text
ok_bound_fallback
```

This does not force-load assets and only runs after a verified SkyBox mesh draw.

Build:

```text
kEnableTodSkyTextureDiscovery = true
kEnableTodSkyAssetLoadProbe = false
kEnableTodTextureMapProbe = false
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 135168 bytes, timestamp 2026-06-03 18:50:48
```

## SkyBox Render-Scope Texture Capture - 2026-06-03

The first natural run after the draw-capture fallback produced no
`tod-sky-textures.tsv`, which means the current VB/IB matcher was still too
narrow for the active sky path. Rather than force-loading sky assets, the ASI
now scopes the already identified TOD sky renderer:

```text
SkyBox::Render @ 0x008F1E10
  validated prologue: 81 EC 90 00 00 00
  inline patch: PUSH Hook_TodSkyBoxRender ; RET
  trampoline: original 6 bytes, then PUSH 0x008F1E16 ; RET
```

The hook preserves behavior by immediately calling the original trampoline. Its
only purpose is to set `g_todSkyBoxRenderDepth` while TOD submits its real
SkyBox render list. During that scope, the existing D3D9 `SetTexture` hook
queues successful stage-0 binds for the normal hash/DDS path.

This is a diagnostic hook, not a content fix:

- No sky path or texture hash is hardcoded into the runtime behavior.
- No `LoadNativeResource` call is made from `Present`.
- No extra draw is emitted.
- Rows with `skyDraw=0` in `tod-sky-textures.tsv` are scoped material binds.
- Rows with `skyDraw>0` remain verified sky-mesh draw matches.
- One-shot status rows (`scope_hook_installed`, `scope_entered`, or a
  `scope_hook_*` failure) are written because the large runtime log is disabled.

Build:

```text
kEnableTodSkyTextureDiscovery = true
kHookTodSkyBoxRenderTextureScope = true
kEnableTodSkyAssetLoadProbe = false
kEnableTodTextureMapProbe = false
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 136192 bytes, timestamp 2026-06-03 19:20:10
```

## SkyBox Render-List Material Capture - 2026-06-03

First scoped run result:

```text
scope_hook_installed
scope_entered
```

No texture rows were emitted, which means `SkyBox::Render` does not bind the
sky texture directly. It only queues render-list commands, and the texture bind
happens later during list execution.

The ASI now also hooks the material opcode writer while inside
`SkyBox::Render`:

```text
FUN_00431660 @ 0x00431660
  opcode 1 writer
  ECX = render list
  stack args = material, stage
  prologue: 56 57 8B F9 8B 47 20
  patch bytes: 7
```

For stage 0, the hook reads the D3D texture pointer from the same field used by
TOD's real flush path:

```text
texture = *(material + 4) & ~3
```

Then it queues that texture for the existing RTX hash/DDS dump code. This is
still diagnostic-only and scene-driven:

- Uses TOD's already selected sky material.
- Does not call `LoadNativeResource`.
- Does not draw synthetic geometry.
- Does not write sky hashes into code or config.

Build:

```text
kEnableTodSkyTextureDiscovery = true
kHookTodSkyBoxRenderTextureScope = true
kEnableTodSkyAssetLoadProbe = false
kEnableTodTextureMapProbe = false
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 137216 bytes, timestamp 2026-06-03 19:31:13
```

## Confirmed SkyBox Texture Config - 2026-06-03

The material-command capture produced ten confirmed SkyBox material textures.
The contact sheet was written to:

```text
re_docs\skybox_material_contact_sheet_20260603.png
```

Texture groups:

- Five `128x128 DXT1` blue skybox faces.
- Five `64x64 A8R8G8B8` horizon/light sky materials.

Applied Remix config:

```text
rtx.skyAutoDetect = 0
rtx.skyBoxTextures = 0x3848EB42A9454B29, 0x38F01A3A44A105E4, 0x3B3C8BFC46BDDD15, 0x3CBFF5A558504B9F, 0x988E3EC036BC1681, 0x98BC8203A806B55D, 0xBFD77BC11E21AE42, 0xDDF1BE54CD1936C8, 0xDF12A2DCB829B60D, 0xE4F08883065FBB37
```

Rationale:

- Local Remix source documents `rtx.skyBoxTextures` as the native way to mark
  draw calls as sky by texture.
- The previous `rtx.skyAutoDetect = 2` path was heuristic and could misclassify
  TOD's scene cameras/draws.
- `0xDDF1BE54CD1936C8` was removed from `rtx.uiTextures` because it is one of
  the captured `128x128 DXT1` SkyBox faces, not UI.
- `rtx.conf` and `scripts\rtx.conf` were made byte-identical for reproducible
  startup behavior.
