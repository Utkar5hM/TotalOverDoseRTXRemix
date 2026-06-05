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

## Second Sky Family Config Expansion - 2026-06-03

After testing mission 1, the sky was visible but looked like the same blue sky
family. The active SkyBox material hook captured an additional mission-1
yellow/horizon family that was not present when `state5` was saved.

Updated contact sheet:

```text
re_docs\skybox_material_contact_sheet_all_20260603.png
```

Applied config expansion:

```text
rtx.skyBoxTextures = 0x062000B7F573945E, 0x164B13483E1BB6C2, 0x1D1D8E3DB8CE7310, 0x2F45551FAFF6107F, 0x3848EB42A9454B29, 0x38F01A3A44A105E4, 0x3B3C8BFC46BDDD15, 0x3CBFF5A558504B9F, 0x4D29CD6D0157132D, 0x6087F9EB4BEC07AE, 0x68B8DC7027BB5EA5, 0x7919EC4E5955B32E, 0x988E3EC036BC1681, 0x98BC8203A806B55D, 0xA55EB28F0D50E1DC, 0xAB767F0AC00773F9, 0xBFD77BC11E21AE42, 0xD9D0D405A049B4A1, 0xDDF1BE54CD1936C8, 0xDF12A2DCB829B60D, 0xE0A8EF0E70BA5ABC, 0xE4F08883065FBB37, 0xE6EC0E321A0D5CA9, 0xFA794A119892B086, 0xFE36AFFA25F09DE5
```

Also removed `0x1D1D8E3DB8CE7310` from `rtx.uiTextures` because runtime capture
now proves it belongs to SkyBox material submission, not UI.

## Runtime SkyBox Mesh Marker - 2026-06-03

Static sky texture lists are incomplete by nature because TOD can load multiple
SkyBox families per mission/scene. The cleaner all-mission path is to identify
TOD's actual SkyBox mesh execution and give Remix an explicit sky signal for
those draws.

Confirmed engine flow:

```text
SkyBox::Render @ 0x008F1E10
  FUN_00431660(renderList, material, stage)
  FUN_00432C70(renderList, mesh)

Render-list execution:
  FUN_004540E0(mesh)
    IDirect3DDevice9::DrawIndexedPrimitive(...)
```

ASI implementation:

- `FUN_00432C70` records mesh pointers only while `SkyBox::Render` is active.
- `FUN_004540E0` raises a SkyBox mesh draw-depth counter only for those
  recorded meshes.
- The D3D9 indexed draw hook sees that counter and temporarily sets viewport
  `MinZ = 0.999` for the actual SkyBox draw.
- The viewport is restored immediately after the D3D call.

Remix config:

```text
rtx.skyAutoDetect = 0
rtx.skyMinZThreshold = 0.99
```

Reasoning:

- `skyMinZThreshold` is documented in local Remix source as explicit sky
  detection.
- This avoids heuristic camera auto-detection and avoids needing every possible
  TOD sky texture hash up front.
- The captured `rtx.skyBoxTextures` list remains useful as fallback/evidence,
  but the runtime mesh marker is the proper general support path.

Build:

```text
scripts\TODCameraResend.asi size: 138752 bytes
scripts\TODCameraResend.asi timestamp: 2026-06-03 20:42:17
```

## 2026-06-03 20:57 - Sky A/B: runtime mesh marker only

Problem:

- With the static `rtx.skyBoxTextures` list enabled, mission 1 still showed the
  blue sky family even after the yellow/orange mission-1 SkyBox materials were
  captured.
- This proved the static hash-list path is too blunt for final support: it can
  identify sky textures, but it does not prove that Remix is consuming TOD's
  actual SkyBox draw sequence in the same order the game renders it.

Runtime-marker test:

- Removed `rtx.skyBoxTextures` from both `rtx.conf` and `scripts\rtx.conf`.
- Kept:

```text
rtx.skyAutoDetect = 0
rtx.skyMinZThreshold = 0.99
```

- The ASI now relies on the reversed TOD SkyBox path:

```text
SkyBox::Render -> render-list mesh command -> RenderMesh::Draw -> D3D DrawIndexedPrimitive
```

- `RenderMesh::Draw` writes a one-shot `sky_mesh_draw_entered` TSV row when one
  of the meshes recorded from `SkyBox::Render` is executed.
- The D3D indexed draw hook writes a one-shot `sky_viewport_marked` or
  `sky_viewport_not_marked` TSV row, with the primitive count and active texture
  metadata, when it sees a TOD SkyBox draw.

Purpose:

- If the next run contains `sky_mesh_draw_entered` and `sky_viewport_marked`,
  the clean runtime path is reaching Remix and any remaining blue/yellow
  mismatch is in Remix sky compositing or TOD's separate haze/sun overlay
  passes.
- If either row is missing, the ASI is not yet marking the real draw and the
  hook path needs correction before config tuning.

Build:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 138752 bytes
scripts\TODCameraResend.asi timestamp: 2026-06-03 20:57:56
scripts\TODCameraResend.asi SHA256 0D514CBAF6DBF0F838AF33C4E0A5B3700D5789AF44E09F0CC3CAB774B385FA87
rtx.conf / scripts\rtx.conf SHA256 0C63F640A32A2EE57353ECC747B46F3D0F55582F0FDF5F7E15116D089ECD1DF3
```

## 2026-06-03 21:10 - Sky still blue: confirmed SkyBox is blue, mission tint is fog/haze

Latest run after removing `rtx.skyBoxTextures`:

- `tod-sky-textures.tsv` contains `sky_mesh_draw_entered`.
- `tod-sky-textures.tsv` contains `sky_viewport_marked`.
- The marked draw uses a 128x128 DXT1 blue SkyBox texture
  (`0x98BC8203A806B55D`) with primitive count 4 / vertex count 4.

Conclusion:

- The ASI is correctly marking TOD's real SkyBox mesh draw.
- In this mission-1 gameplay state, TOD's actual SkyBox texture is blue.
- The original yellow/orange mission appearance is therefore not a different
  cube texture selected by `SkyBox::Render`; it is TOD's environment fog/haze /
  sun grading layered over the blue SkyBox and world.

Remix source check:

- `RtxOptions.md` documents fixed-function fog support:
  `rtx.enableFog`, `rtx.fogIgnoreSky`, and `rtx.fogColorScale`.
- `SceneManager::submitDrawState()` stores the first unreplaced non-empty fog
  state each frame.
- `D3D9Rtx::PrepareDraw()` only clears fog on sky draw calls when
  `rtx.fogIgnoreSky` is enabled.

Config A/B:

```text
rtx.enableFog = True
rtx.fogIgnoreSky = True
rtx.fogColorScale = 1.0
```

Reasoning:

- Because the confirmed SkyBox is drawn first and is now correctly categorized
  as Sky, `rtx.fogIgnoreSky = True` forces Remix to take fog from later world
  draws instead of the sky draw.
- `rtx.fogColorScale = 1.0` preserves TOD's authored fog color instead of the
  Remix default 0.25 multiplier.
- This is config-only and uses Remix's native D3D9 fog path; no texture hashes,
  mission names, or content exceptions are added to the ASI.

Config hash:

```text
rtx.conf / scripts\rtx.conf SHA256 0E0C52B88267ECF744BBC0EAA14109A6361E7219D1E6A795C29303F0E21DEE84
```

## 2026-06-03 22:30 - Sky marking proven COMPLETE; warmth is atmospheric, not a sky bug

User report: "only one (blue) sky across all missions; should look warm." Earlier
working theory was that warm sky layers were drawn through an unmarked path and
Remix relit them. A diagnostic was added to test that theory, then it was
disproven.

### Diagnostic added: `sky_draw_marked` rows

Problem with the prior `tod-sky-textures.tsv`: every `ok` row was a *material
bind* (`skyDraw=0`). The per-texture dedup (keyed on the source texture pointer /
rtx hash) collapsed the actual marked-*draw* entries (`skyDraw>=1`) into that
earlier material-bind record, so the log could not show which sky textures
actually reached a viewport-MinZ-marked sky draw.

ASI change (`tod_camera_resend_asi/TODCameraResend.cpp`):

- `TodSkyTextureRecord` gained a `drawnLogged` flag.
- `QueueTodSkyTexture` now lets marked-draw entries (`skyDraws>=1`) through the
  dedup even when the texture was already recorded as a material bind; it only
  skips an already-drawn-logged texture or a draw already pending this batch
  (`TodSkyDrawPending`).
- `ProcessQueuedTodSkyTextures` emits a separate `sky_draw_marked` row the first
  time a texture is confirmed to reach a MinZ-marked sky draw, independent of the
  `ok` material-bind dedup.

Build: `scripts\TODCameraResend.asi` size 139264, timestamp 2026-06-03 22:03:06.

### Result (airstrip "Get to the plane" mission)

`tod-sky-textures.tsv` after one run:

```text
sky_draw_marked  0x98BC8203A806B55D  128x128 DXT1   (blue cloud)
sky_draw_marked  0xDF12A2DCB829B60D  128x128 DXT1   (warm gradient)
sky_draw_marked  0xDDF1BE54CD1936C8  128x128 DXT1   (blue cloud)
sky_draw_marked  0x38F01A3A44A105E4  128x128 DXT1   (blue cloud)
sky_draw_marked  0x3B3C8BFC46BDDD15  128x128 DXT1   (blue cloud)
sky_draw_marked  0xE4F08883065FBB37  64x64   A8R8G8B8 (warm/golden)
sky_draw_marked  0xBFD77BC11E21AE42  64x64   A8R8G8B8
sky_draw_marked  0x3848EB42A9454B29  64x64   A8R8G8B8
sky_draw_marked  0x3CBFF5A558504B9F  64x64   A8R8G8B8 (warm)
sky_draw_marked  0x988E3EC036BC1681  64x64   A8R8G8B8 (warm/golden)
```

The bound-but-not-drawn (`ok`-only) set was **empty**.

Conclusions:

1. Sky marking is complete and correct. Every bound sky texture reaches a
   MinZ-marked draw. The runtime SkyBox mesh marker works; widening it is
   unnecessary. The "drawn through an unmarked path" theory is disproven.
2. The game's warm dawn/dusk sky faces exist, are loaded, and ARE marked as sky
   (visible in `re_docs/skybox_material_contact_sheet_all_20260603.png`: the
   golden/orange gradients alongside the blue cloud faces).
3. The skybox is a cube: blue cloud faces near the zenith, warm faces near the
   horizon. Both user screenshots aim the camera UP (zipline), showing the blue
   zenith faces; the warm horizon faces are below the treeline / out of frame.
   That is a large part of why it reads as "just blue."
4. The remaining warmth gap vs original is Remix re-lighting/tonemapping
   neutralizing TOD's stylized warm grade, plus TOD's fog/atmosphere. That is the
   postfx/fog domain, NOT the skybox, which is now cleared.

### TOD fog system confirmed in engine (Ghidra)

String scan of `TOD.exe` confirms a real fog system, so the Remix fog lever is
legitimate (not a dead end):

```text
009b44b8 CMD_ENABLEFOG
009b44a0 CMD_SETFOGPROPERTIES
009b3fb4 CMD_PUSH_FOG / 009b3f9c CMD_PUSH_FOGPROPERTIES
009b3d88 CMD_POP_FOG  / 009b3d70 CMD_POP_FOGPROPERTIES
009c250d PCGFOG_Controller
009c9874 Trigger_Activate_Fog
009d15b8 fog_type
009b66fc "CAPS: RangeFog = %i"
009b6714 "CAPS: FogAndSpecularAlpha = %i"
```

The `CAPS: RangeFog` / `FogAndSpecularAlpha` cap checks strongly imply TOD uses
D3D9 fixed-function fog, which Remix can re-apply via `rtx.enableFog`.

Note: the current `rtx.fogIgnoreSky = True` (left from the 21:10 pass) tells Remix
to discard fog on sky draws. Now that warmth is confirmed to live partly in the
sky draws, that setting may be working against the goal and should be revisited
from fog-state evidence rather than guessed.

### Diagnostic added: fog-state logger (instrument before tuning fog)

To avoid tuning Remix fog blind, the ASI now logs TOD's actual fixed-function fog
render-states. `Hook_SetRenderState` (already installed) calls a new
`LogTodFogState` for `FOGENABLE`, `FOGCOLOR`, `FOGTABLEMODE`, `FOGSTART`,
`FOGEND`, `FOGDENSITY`, `RANGEFOGENABLE`, `FOGVERTEXMODE`. Rows are deduped on
changed value and tagged with `skyScope` (whether inside `SkyBox::Render`).

```text
kEnableTodFogStateLog = true
output: rtx-remix\logs\tod-fog-state.tsv
columns: row  fogState  rawValue  decoded  skyScope  setRenderStateCall
```

This tells us, conclusively: does TOD set fixed-function fog at all, what
`FOGCOLOR` (warm?), and on which draws (sky vs world). From that we set Remix fog
(`rtx.enableFog`, `rtx.fogIgnoreSky`, `rtx.fogColorScale`) correctly instead of by
trial and error.

Build: `scripts\TODCameraResend.asi` size 140288, timestamp 2026-06-03 22:30:31.

Next run: play one mission (ideally a warm/dusk one), quit, then read
`tod-fog-state.tsv`. If TOD sets a warm `FOGCOLOR` with `FOGENABLE=1`, configure
Remix fog from those values. If TOD sets no fixed-function fog, the warmth is a
tonemapping/grade problem and the fix moves to Remix postfx/exposure instead.

## 2026-06-03 ~22:50 - Fog-state result: TOD atmosphere IS fixed-function fog

User played one mission (jungle/airstrip). `tod-fog-state.tsv` is conclusive.

TOD heavily uses D3D9 fixed-function fog, ping-ponging two states per frame:

```text
World pass (the atmosphere):
  FOGENABLE = 1
  FOGCOLOR  = 0x00777952  -> RGB(119,121,82)  green/sage haze   (dominant, 1189x)
  FOGSTART  = 10.0   FOGEND = 1400.0   (linear distance fog)
  RANGEFOGENABLE = 1   FOGVERTEXMODE = 3 (D3DFOG_LINEAR, vertex fog; FOGTABLEMODE unset)

UI/2D pass (reset):
  FOGENABLE = 0
  FOGCOLOR  = 0xffffffff (white)   FOGSTART = 1   FOGEND = 100   (2500x)

Other observed world FOGCOLORs (atmosphere drifts per area/time):
  RGB(148,173,136), RGB(171,191,154), RGB(147,172,135), RGB(145,170,133)  (all green/sage)
  RGB(0,0,0) black (223x, certain passes)
Some passes also use dynamic FOGSTART/FOGEND (e.g. 43.6..1231.9, 22.0..1339.7).
```

Key conclusions:

1. TOD's per-mission atmosphere = its fixed-function fog. `rtx.enableFog` is the
   correct, legitimate lever (not a guess). This jungle mission's authored
   atmosphere is a green/sage distance haze, NOT orange. The orange "Job done,
   pal." reference is a different (sunset) mission whose FOGCOLOR will be warm -
   confirming the game DOES vary atmosphere per mission, via fog color.
2. **Every fog state change has `skyScope=0`** - TOD never sets fog inside
   `SkyBox::Render`. So the previous `rtx.fogIgnoreSky = True` was actively
   removing the atmosphere from the sky and leaving a clean blue sky over a
   lightly-hazed world. That is the main reason it read as "blue, not atmospheric."
3. The fog is vertex linear fog over a long range (10..1400), so it ramps in
   gradually; near/mid geometry is only lightly tinted, which is why the effect
   looked subtle even where it was applied.

### Config change applied (evidence-based, config-only)

Both `rtx.conf` and `scripts\rtx.conf`:

```ini
rtx.enableFog = True
rtx.fogIgnoreSky = False   ; was True - let TOD's atmosphere also tint the sky
rtx.fogColorScale = 1.0
```

Rationale: fog is the atmosphere and it is set only on world draws, so the sky
escaped it under `fogIgnoreSky = True`. Setting it `False` makes Remix keep the
active fog on sky draws too, unifying sky + world into TOD's authored atmosphere
(green haze here, warm in sunset missions). No ASI rebuild; the fog-state logger
stays on for now so the next run still records fog values.

Next run: test BOTH this jungle mission (expect green/sage haze over the whole
frame incl. sky) and a known sunset mission (expect warm haze) to confirm the
atmosphere now comes through and varies per mission. If the sky goes too
washed/white, the cause is Remix capturing the white `FOGENABLE=0` reset state at
sky-draw time, and the next step is to gate fog so only the enabled world state is
used.

## 2026-06-03 ~23:10 - fogIgnoreSky was a no-op; volumetric fog remap is the lever

User: "i dont see a change" after `fogIgnoreSky = False`. Verified the active
`scripts\rtx-remix\logs\remix-dxvk.log` loaded the new value, so it was applied -
it simply does nothing visible. Traced why through the cloned Remix source at
`C:\Users\utkar\AppData\Local\Temp\dxvk-remix-src`.

How Remix consumes D3D9 fog:

- `d3d9/d3d9_rtx_utils.cpp setFogState()` captures fog when `D3DRS_FOGENABLE` is
  set, and for the mode uses `FOGTABLEMODE` if non-NONE else `FOGVERTEXMODE`. TOD
  uses vertex linear fog, so Remix DOES capture it (mode=LINEAR, color olive,
  scale=1/(end-start), end=1400). Vertex fog is not the problem.
- `rtx_scene_manager.cpp submitDrawState()` picks `m_fog` = the first per-frame
  draw whose fog mode != NONE (reset each frame). TOD world draws qualify.
- `RtxOptions::fogIgnoreSky` (`rtx_options.h:1200`) doc: "If true, sky draw calls
  will be skipped when searching for the D3D9 fog values." It ONLY changes which
  draws are searched for fog values - it does NOT make the sky receive fog. TOD's
  sky draws have `FOGENABLE=0` (mode NONE) so they are skipped regardless. Hence
  flipping it is a no-op for TOD. (Remix default is False anyway.)

Two real fog paths in Remix:

1. `rtx.enableFog` + `rtx.fogColorScale` (`rtx_composite.cpp:386`): composite
   depth-fog. `fogColor = fog.color * colorScale`, linear over the captured
   start/end. With TOD's range 10..1400, near/mid geometry gets <~15% tint, and
   the sky is composited via a separate sky buffer so it never receives this fog.
   -> faithfully applied but visually weak. This is why it looked unchanged.
2. Global volumetric fog remap (`rtx_global_volumetrics.cpp`): converts the game's
   fixed-function fog into Remix's physical/volumetric scattering that envelops the
   whole scene including how sky/sun read through the atmosphere. Off by default.
   Gated by `enableFogRemap` + `fogState.mode != NONE` + `shouldConvertToPhysicalFog`.
   For TOD: approximate density ~0.005 < `waterFogDensityThreshold` 0.065 -> the
   gate passes, so TOD's fog DOES qualify for remap.

### Config change applied (both rtx.conf)

```ini
rtx.enableFog = True
rtx.fogIgnoreSky = False            ; no-op for TOD, left at Remix default
rtx.fogColorScale = 1.0
rtx.volumetrics.enableFogRemap = True       ; was off - the real atmosphere lever
rtx.volumetrics.enableFogColorRemap = True  ; use TOD's per-mission fog color
```

`rtx.volumetrics.enable` is already True by default. `enableFogMaxDistanceRemap`
stays at its True default. The misleading earlier note that `fogIgnoreSky=False`
unifies sky+world was corrected in the rtx.conf comments.

Next run: with volumetric fog remap on, expect a real atmospheric haze in TOD's
per-mission color (green here, warm in sunset levels). If it is too thick/thin,
tune `rtx.volumetrics.fogRemapTransmittanceMeasurementDistanceMin/MaxMeters`
(currently 20/100 m) and `transmittanceMeasurementDistanceMeters`. If still no
visible atmosphere, confirm `rtx.volumetrics.enable = True` is active and that
global volumetrics are not being suppressed by the graphics preset.

### Honest framing

Sky and fog are now both proven correct/captured. Remaining "not warm like the
original" is fundamentally that Remix path-traces with realistic lighting and
neutralizes TOD's stylized rasterizer grade. Volumetric fog remap is the
legitimate engine-intended way to bring back the atmosphere; beyond that, matching
the exact stylized look is Remix tonemapping/post/lighting (a creative grade),
not a correctness bug.

## 2026-06-03 ~23:30 - Real problem is EXPOSURE (too dark), not fog

User: volumetric remap "making everything darker," with an RTX-vs-original
comparison set in `screenshots/sunsky`.

Comparison (RTX shots have the FPS/GPU overlay; originals do not):

- Original (non-RTX): bright, vibrant - green foliage well lit, warm yellow/orange
  sunset gradient at the horizon, clear daytime. This is TOD's flat-lit look.
- RTX: dark and muddy overall; shadowed areas crushed to near-black; the sun reads
  as a blown-out orange glare that also LEAKS THROUGH walls/roofs into interiors
  (e.g. the warehouse shot) because TOD geometry is thin/single-sided and
  path-traced sunlight bleeds through.

Root cause of the darkness found in `rtx.conf`:

```ini
rtx.postfx.enable      = False
rtx.autoExposure.enabled = False   ; <-- disabled during old black-screen debug, never restored
rtx.bloom.enable       = False
```

With auto-exposure OFF, Remix holds a fixed low exposure, so path-traced scenes
stay dark while the original (rasterizer output shown directly) is bright. The
volumetric fog remap added scattering/absorption on top of an already-underexposed
image, which is why it read as "even darker."

### Changes applied (both rtx.conf, config-only)

- REVERTED `rtx.volumetrics.enableFogRemap` / `enableFogColorRemap` (wrong lever
  for "too dark"). Kept the faithful, subtle `rtx.enableFog` composite fog.
- ENABLED `rtx.autoExposure.enabled = True` to lift overall brightness toward the
  original.

Two separate issues remain to evaluate after this run:

1. Brightness/exposure - if auto-exposure overshoots or the bright sun makes it
   clamp down, tune `rtx.autoExposure` (evaluation region / target) or add a
   manual exposure/tonemapping bias. May also re-enable `rtx.bloom` for the warm
   sun glow once exposure is balanced.
2. Sun light leaking through thin single-sided geometry - a geometry/lighting
   artifact (TOD walls/roofs are single-sided with no thickness). Candidate fixes:
   Remix backface/secondary-ray opacity or `rtx` thin-geometry/particle/opacity
   handling, or marking those draws appropriately. Separate from exposure.

## 2026-06-03 ~23:50 - Lighting model mismatch: baked vs path-traced; auto-exposure reverted

Resolution tell (confirmed via `file` on screenshots/sunsky): 2560x1440 = RTX
(Remix render res), 1600x1200 = original (native TOD res; the bright/warm shots,
GPU ~2% = no ray tracing). So the warm sunset shots are the rasterized original.

User art notes:
- After enabling auto-exposure the sky went BLUE (washed). Before, the sky color
  matched (but everything was dark).
- In the original the sun "flows with the sky, does not emit light like a bulb" -
  it is a soft glow painted into the sky texture (a backdrop), not a light.

Diagnosis (the real model mismatch):
- Original TOD = flat BAKED lighting in vertex colors; sun is a painted sky glow.
  Bright, low-contrast, sun casts nothing.
- RTX = path-traced. Two defaults break the original look:
  1. `rtx.vertexColorStrength` default 0.6 -> Remix uses only 60% of TOD's baked
     vertex lighting, so the world is dimmer than the original. THIS is the main
     "everything darkened" cause (not exposure).
  2. The bright sun region of the sky becomes a real light via the sky env probe
     (`rtx.skyBrightness`, `rtx.skyProbeSide`) and TOD's additive sun-flare/overburn
     sprites get translated to emissive (`rtx.enableEmissiveBlendModeTranslation`),
     so the sun "emits like a bulb", blows out, and leaks through thin geometry.
- Auto-exposure was the wrong brightness lever: it adapts to average luminance and
  washes the sky.

Changes applied (both rtx.conf):
- REVERTED `rtx.autoExposure.enabled` back to False (it washed the warm sky).
- `rtx.vertexColorStrength = 1.0` (was default 0.6) - use TOD's full baked lighting
  to brighten the world without touching the sky. Rule-compliant (global render
  option, not TOD content).

### Recommended: live-tune the rest in the Remix menu (Alt+X)

The remaining work (overall brightness fine-tune + taming the bulb sun) is
subjective look tuning; live sliders beat blind config round-trips. Key knobs:

```text
rtx.vertexColorStrength    1.0  - baked lighting amount (world brightness/flatness)
rtx.userBrightness         50   - final image brightness slider [0..100], clean manual
                                  brightness (better than auto-exposure; no sky wash)
rtx.skyBrightness          1.0  - lower to reduce the sun-as-bulb sky lighting
rtx.emissiveIntensity      1.0  - global emissive scale (sun flare / overburn sprites)
rtx.emissiveBlendOverrideEmissiveIntensity 0.2 - additive->emissive intensity; lower
                                  to stop additive sun sprites acting like lights
```

Suggested live recipe to approach the original: raise vertexColorStrength to taste,
use userBrightness for final level (avoid auto-exposure), then lower skyBrightness /
emissive override until the sun reads as a soft backdrop glow instead of a bulb.
Bake the chosen values into both rtx.conf afterward.

The sun-leak-through-walls remains a separate thin-single-sided-geometry artifact.

## 2026-06-04 ~00:10 - vertexColorStrength fixed brightness; OG sky color IS the fog (volumetric remap re-enabled)

Run with `vertexColorStrength = 1.0` + auto-exposure off:

- Brightness FIXED. Scene is bright/vibrant again, and the sunset orange now shows
  at the horizon (RTX 2000x1125 shots 23.19.10 / 23.19.12, GPU 92% = RT on).
  vertexColorStrength was the correct "too dark" lever (TOD bakes lighting into
  vertex colors; 0.6 default discarded 40%).

Key correction from the user's OG looking-up shot (1600x1200, GPU 2% = raster):

- The OG sky is NOT blue even at the zenith - it is GREEN-GREY, matching the jungle
  fog color RGB~119,121,82. The OG sky color comes FROM TOD's per-mission
  atmospheric fog. RTX showed blue because the composite fog does not touch the sky,
  leaving the bare blue skybox faces visible with no atmosphere over them.

User requirement: this must be AUTOMATIC per mission (many missions; manual
`skyBrightness` per mission is the wrong approach - agreed).

Resolution: the volumetric fog remap is the automatic, game-driven mechanism - it
converts TOD's per-mission fog into atmospheric scattering that tints the whole
scene INCLUDING the sky, so green jungle / orange sunset / etc. follow the game's
own fog with zero manual tuning. It was reverted earlier on the false assumption it
caused the darkness; the darkness was actually `vertexColorStrength = 0.6`. With
that fixed at 1.0, volumetric remap was RE-ENABLED:

```ini
rtx.vertexColorStrength = 1.0
rtx.autoExposure.enabled = False
rtx.volumetrics.enableFogRemap = True
rtx.volumetrics.enableFogColorRemap = True
```

Next run: check whether the sky now takes the per-mission fog tint (green-grey in
jungle, orange at sunset) automatically AND stays bright enough with
vertexColorStrength=1.0 compensating. If still too dark, tune volumetric density /
`rtx.volumetrics.transmittanceMeasurementDistanceMeters` rather than reverting.

UI note for the user: the Remix menu (Alt+X) has a search box at the very top of
the developer window; type an option name (e.g. "skyBrightness") to filter. But all
tuning here is being kept in rtx.conf so nothing needs to be set by hand in-game.

## 2026-06-04 ~00:30 - Volumetric sky works; thin the fog + soften sun flare

Run with volumetric remap on (shots 23.29/23.30):

- WIN: the warm atmospheric sky now renders automatically (sunset shot 23.29.53 has
  a proper orange sky), driven by TOD's per-mission fog. The automatic, game-driven
  sky-atmosphere goal is met.
- Side-effect 1: volumetric fog darkens the NEAR world (hangar/ground shot 23.30.27
  too dark). Fog is optically too thick up close.
- Side-effect 2: the sun reads as a hard bright disc + star/cross lens-flare
  (23.29.53), not OG's soft glow. This is TOD's additive sun_flare/sun_flare_star /
  Sun_OverBurn sprites being translated to emissive (enableEmissiveBlendModeTranslation).

Targeted tunes (both rtx.conf):

```ini
rtx.volumetrics.fogRemapTransmittanceMeasurementDistanceMinMeters = 60   ; was 20
rtx.volumetrics.fogRemapTransmittanceMeasurementDistanceMaxMeters = 300  ; was 100
rtx.emissiveBlendOverrideEmissiveIntensity = 0.1                          ; was 0.2
```

Rationale:
- Raising the transmittance measurement distance thins the fog so near geometry is
  less darkened; the sky (≈infinite distance) still tints fully, so the atmosphere
  is kept. These are starting values to refine from the next screenshots.
- Lowering the additive->emissive intensity softens TOD's flare sprites so the sun
  blends rather than blasting. Caveat: global to all additive blends (fire, muzzle
  flashes), so kept moderate (0.1, not 0).

Next: if the world is bright enough but the sky tint weakened, lower the
transmittance max back toward ~200; if the sun is still a hard disc, drop emissive
override toward 0.05 (watch fire/muzzle don't go flat). The sun-as-a-distinct-disc
may also need the sky/sun env-light handling if the sprite tune is insufficient.

## 2026-06-04 ~00:50 - Hard requirements: sun must not be a light; world must be bright

User, emphatic: (1) the sun must NOT be a light source / not spherical - it must
blend into the sky; (2) the world is still too dark to see enemies (gameplay
blocking). Key reference: Image #5 (vertexColorStrength=1.0, BEFORE volumetric) was
bright/vibrant - proving the volumetric fog is the darkener, not the base lighting.

Two decisive changes (both rtx.conf):

```ini
rtx.volumetrics.fogRemapTransmittanceMeasurementDistanceMinMeters = 500   ; was 60
rtx.volumetrics.fogRemapTransmittanceMeasurementDistanceMaxMeters = 2000  ; was 300
rtx.emissiveBlendOverrideEmissiveIntensity = 0.0                          ; was 0.1
```

Rationale:
- Pushing the transmittance distance very high makes near-ground fog negligible (so
  the world returns to Image #5 brightness and enemies are visible) while the sky,
  at ~infinite distance, still tints fully (T = exp(-d/D): at d=50m, D=2000m -> ~2.5%
  fog near; at d=inf -> 100% fog color on the sky). Keeps atmosphere, drops the
  world-darkening.
- emissive override 0.0 stops TOD's additive sun-flare/sun_flare_star/Sun_OverBurn
  sprites from emitting, so the spherical sun + lens-flare rays go away and only the
  soft painted sky-sun remains. CAVEAT: global to all additive blends, so fire /
  muzzle / explosion glow is also killed - flagged to the user to check; if dull,
  raise toward ~0.03-0.05 for a middle ground.

## 2026-06-04 ~02:00 - Reverted sun whack-a-mole; researched the PROPER Remix sun

User stopped the per-tweak approach ("shouldn't there be a cleaner way? research RTX
Remix, reverse-engineer the game if required"). Reverted all sun-specific changes:
`rtx.hideInstanceTextures`, `rtx.emissiveBlendOverrideEmissiveIntensity`, and an
unbuilt ASI additive-sprite capture diagnostic. Kept brightness (vertexColorStrength
= 1.3, auto-exposure off) and the volumetric atmosphere.

### Root cause (why the sun was never going to work via config)

NVIDIA RTX Remix docs + the cloned dxvk-remix source make it clear:

- The proper way to represent a sun in Remix is a real LIGHT - a Distant
  (directional) light. NVIDIA: "Distant Lights represent directional light from an
  infinite distance and are commonly used to simulate the Sun." Scene illumination
  should come from primitive lights, not emissive meshes / sprites.
- TOD has NO real sun light. D3DRS_LIGHTING is off (lighting baked into vertex
  colors; confirmed by the candidates TSV `lighting=0` column). The on-screen "sun"
  is only (a) a glow painted into the skybox texture and (b) a camera-facing 2D
  billboard sprite (sun_flare / Sun_OverBurn). A camera-facing billboard is why it
  reads as a disc "that goes everywhere".
- So Remix had no actual light to ray-trace for the sun, and every config knob we
  tried (skyBrightness, emissive, hide) was treating a symptom.

### Confirmed viable: inject a D3D9 directional light, Remix ray-traces it

In the cloned source `src/d3d9/d3d9_device.cpp:1842`, `D3D9DeviceEx::SetLight` does:

```cpp
m_state.lights[Index] = *pLight;
if (m_state.IsLightEnabled(Index)) {
  m_flags.set(D3D9DeviceFlag::DirtyFFVertexData);
  m_rtx.SetDirty(D3D9RtxFlag::DirtyLights);   // "implement light support in d3d9 rtx"
}
```

So a D3D9 light that is SET and ENABLED is picked up by the RTX path - and it keys on
`IsLightEnabled(Index)`, not on the D3DRS_LIGHTING render state. That means the ASI
can enable a directional light purely for Remix without changing TOD's rasterized
output (TOD ignores it because its LIGHTING is off).

`d3d9_fixed_function.cpp:1118` references `D3DLIGHT_DIRECTIONAL`, and Remix converts
directional lights to distant lights.

### Clean plan (principled, automatic) - proposed, awaiting go

1. ASI injects one D3D9 directional light: `SetLight(idx, dirLight)` +
   `LightEnable(idx, TRUE)`, refreshed per frame. Remix ray-traces it as the sun
   (real sunlight + shadows). No billboard needed.
2. Hide the 2D sun billboard sprite (so the disc is gone), leaving the soft skybox
   glow as the visible sun.
3. Reverse-engineer TOD's sun-direction vector (the value that positions the skybox
   sun / lens flare) so the injected light's direction + color follow the game's sun
   automatically per mission / time of day. First pass can use a fixed direction to
   validate Remix picks the light up, then wire it to the RE'd sun vector.

Alternative (NVIDIA's standard manual workflow): use the RTX Remix Toolkit to author
a USD mod that adds a Distant Light and hides the sun sprite - clean but per-scene
and manual, not automatic across missions.

## 2026-06-04 ~00:50 - Step 1 sun light CONFIRMED; fog color remap off (over-orange)

ASI now injects a D3D9 directional light each frame in `Hook_Present`
(`InjectSunLight`, flag `kInjectSunLight`, index 0, SetLight + LightEnable). Source
already confirmed Remix consumes it: `d3d9_rtx.cpp:376` on `DirtyLights` iterates
`enabledLightIndices` -> `addLights`, with NO `D3DRS_LIGHTING` gate. User result with
a deliberately bright (4,4,4) validation light: "increased brightness overall" =
CONFIRMED Remix ray-traces our injected sun. (Note: the ASI's verbose `Log()` is
compiled out via `#if !TOD_ENABLE_RUNTIME_LOG`, so InjectSunLight won't appear in
tod-camera-resend.log - that's expected; the injection still runs.)

After light tuned to a natural warm sun (2.0, 1.8, 1.5). Direction is still the fixed
test vector (0,-0.8,0.6) - step 2 will wire it to the RE'd TOD sun direction.

New user feedback: with the volumetric fog COLOR remap on, missions 2/3 (warm/desert)
went heavily orange; mission 2 should be bright with light haze and only slightly
warm (ref: desert-highway original). Fix: `rtx.volumetrics.enableFogColorRemap = False`
in both rtx.conf - keep a light neutral haze (enableFogRemap stays True), and let the
injected sun light + skybox carry per-mission warmth instead of the fog tinting
everything. The billboard sun sprite is still present (un-hidden) and still shows as
a disc - to be removed in step 2/3.

Next:
1. RE TOD's sun-direction vector so InjectSunLight points the light to match the
   real sun per mission (Ghidra: find the sun/skybox/lens-flare direction global).
2. Hide the sun billboard sprite cleanly (its hash, or a render-behavior skip).
3. Final balance of sun intensity vs vertexColorStrength vs neutral haze.

## 2026-06-04 ~01:10 - Fog balance + render distance

- Mission 1 sky went blue with fog color remap OFF (its warm sky comes from the fog,
  not its blue-cube skybox). Re-enabled `enableFogColorRemap = True` but LIGHTENED the
  fog a lot: `fogRemapTransmittanceMeasurementDistanceMin/Max = 1500 / 6000` (was
  500/2000) so m1 keeps warmth while m2/3 only get a light "a bit orangey" tint.
  This is a documented COMPROMISE - the real per-mission sky fix is the skybox
  compositing (why Remix shows the blue cube face instead of TOD's warm faces), TBD.
- Render distance: `kTodCameraFarClipOverride` 3000 -> 8000 (sane cap 10000) per user
  ("it should also be far") so distant terrain (desert vistas) renders. ASI 140288
  @01:12:01.

## 2026-06-04 ~02:00 - PROPER fog method: base volumetric, not the legacy remap

After many fog tweaks ping-ponging blue<->orange, researched the proper way (NVIDIA
Volumetrics docs + dxvk-remix `rtx_global_volumetrics.h`). Root insight:

- The legacy fog REMAP (`enableFogRemap`/`enableFogColorRemap`) takes the game's fog
  and fogs the WHOLE scene including the SKY. The sky is at infinite distance, so it
  accumulates the FULL fog in-scatter color there - which is why the sky went solid
  orange/green and the transmittance-distance knob never changed the sky (it only
  affects the world, never the infinitely-far sky).
- The PROPER way is the BASE volumetric (`rtx.volumetrics.enable=true`) with manual
  params, NOT the remap. The base volumetric only fills the froxel grid out to
  `rtx.volumetrics.froxelMaxDistanceMeters` (DEFAULT 20m - that is why turning the
  remap off gave "no fog at all"). The SKY is beyond the grid, so it keeps its real
  skybox color while the WORLD gets a light haze.

Base volumetric option reference (dxvk-remix source):

```text
rtx.volumetrics.enable                              true
rtx.volumetrics.transmittanceColor                 default 0.999^3 (near white) - haze color
rtx.volumetrics.transmittanceMeasurementDistanceMeters  default 200 - density (HIGHER = lighter)
rtx.volumetrics.singleScatteringAlbedo             default 0.9
rtx.volumetrics.anisotropy                         default 0
rtx.volumetrics.froxelMaxDistanceMeters            default 20 - haze REACH (raise to cover world)
```

Applied (both rtx.conf), config-only:

```ini
rtx.enableFog = False                 ; drop the separate composite depth-fog (one system)
rtx.volumetrics.enableFogRemap = False
rtx.volumetrics.enableFogColorRemap = False
rtx.volumetrics.enable = True
rtx.volumetrics.transmittanceColor = 0.94, 0.92, 0.87   ; light warm/dusty haze
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 800   ; light density
rtx.volumetrics.froxelMaxDistanceMeters = 3000          ; reach across the world
```

Expected: light dusty haze over the world, SKY keeps its real skybox color (m1 blue,
m2 desert), no orange wash. Tuning: density via transmittanceMeasurementDistanceMeters
(higher=lighter), reach via froxelMaxDistanceMeters, color via transmittanceColor.
Trade-off vs the remap: this haze is a single global color (not per-mission), but the
SKY stays correct - which is what the user wanted. Per-mission sky color now depends
on TOD's actual skybox (m1 is genuinely blue without fog; its green was fog-only).

## 2026-06-04 ~19:30 - Long-standing displaced/flicker glitch = geometry hash includes positions

User: a long-standing glitch (NOT fog/haze) - during motion, distant trees/foliage
appear as scattered/displaced shapes flung into the sky, worst at low angles toward
the bright sky. Pre-dates all the sky/fog work.

Diagnostic 1 (camera resend): added `kEnableCameraResend` toggle and built with it
OFF. Result: glitch got WORSE (geometry more distorted across many frames). So the
ASI camera-resend STABILIZES the camera and is NOT the cause. Reverted to enabled.

Root cause (internet + dxvk-remix source):
- rtx-remix issue #198 "Flickering geometry causes unstable motion vectors"; NVIDIA
  note: software (CPU) animation flickers, hardware animation is stable.
- Remix tracks geometry across frames via a hash. `rtx_hashing.h`: `FullGeometryHash`
  = VertexDataHash | TopologicalHash, and VertexDataHash includes
  `HashComponents::VertexPosition`. The per-frame generation rule
  (`rtx.geometryGenerationHashRuleString`) defaults to
  `positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader`.
- TOD rebuilds distant tree/foliage billboards on the CPU each frame (re-facing the
  camera) and does software skinning (RE: `FUN_00462690` "trying to SW skin a mesh").
  So vertex POSITIONS change every frame -> the geometry hash changes every frame ->
  Remix sees brand-new geometry each frame -> no temporal history -> unstable motion
  vectors -> scattered/displaced flicker, worst during motion. Structural, hence
  long-standing.

Fix applied (both rtx.conf), config-only:

```ini
rtx.geometryGenerationHashRuleString = indices,texcoords,geometrydescriptor,vertexlayout,vertexshader
```

Drops `positions` so geometry is tracked by topology/UVs (stable across frames for
CPU-animated/billboarded meshes).

CRASH - this fix is INVALID. Removing `positions` from
`geometryGenerationHashRuleString` crashes Remix on the CS thread:
`err: Position hash should never be empty`. Remix hard-requires `positions` in the
GENERATION rule. Reverted both rtx.conf (removed the line -> default restored).

So the clean global geometry-hash lever is unavailable. The diagnosis stands (CPU
billboards/SW-skin re-hash every frame), but the displaced-into-SKY symptom is also a
TRANSFORM error (geometry projected to the wrong place), not pure temporal flicker -
consistent with the earlier "Remix billboard-format limitation with this game's distant
LOD" note (Remix `createBillboards` rejects TOD's quad index layout -> raytraces raw
billboard geometry). `rtx.neeCache.enableReshuffleResilience` is already ON by default.
Remaining realistic options:
- `rtx.antiCulling.object.enable = True` + `hashInstanceWithBoundingBoxHash = False`
  (documented anti-flicker for games with primitive culling) - low-risk test, but may
  not address the displacement specifically.
- Identify the distant-foliage billboard texture(s) and `rtx.hideInstanceTextures`
  them (foliage disappears but no glitch).
- Deep ASI fix: remap TOD's billboard index buffers to Remix's A,B,C,A,C,D layout so
  Remix accepts them as real billboards (principled but involved).
- Accept as an inherent Remix<->TOD billboard limitation.

### 2026-06-04 ~20:00 - Captured TOD's quad index layout; implemented the remap (option 3)

ASI diagnostic `CaptureBillboardIndices` (flag `kCaptureBillboardIndices`) read the
index buffers of alpha-blended/tested indexed triangle-list quad batches and logged
them to `rtx-remix/logs/tod-billboard-indices.tsv`. Result:

```text
row blend atest zwrite primCount numVerts indexed fmt16 ABCACD idx0_11
1   1     1     1      546       482      1       1     0      0,1,2,2,3,0,4,0,3,3,5,4
2   1     1     0      44        23       1       1     0      1,0,2,2,3,1,1,4,5,5,6,1
```

CONFIRMED: TOD triangulates every quad as `A,B,C, C,D,A` (invariant i3==i2, i5==i0),
16-bit indices, triangle list. Remix's `createBillboards` (rtx_instance_manager.cpp
~2039) requires `A,B,C, A,C,D` (i0==i3 && i2==i4) and aborts otherwise -> raytraces raw
billboard geometry -> displaced/spiky foliage. The two layouts are the SAME two
triangles (same verts; (C,D,A) is a rotation of (A,C,D)), so the remap is lossless.

Fix implemented (`RemapBillboardIndices`, flag `kRemapBillboardIndices`): before each
alpha indexed triangle-list quad draw, lock the used index range (16-bit) and rewrite
each quad per `i3=i0; i4=i2; i5=i4` (A,B,C,C,D,A -> A,B,C,A,C,D). Idempotent (skips
quads already in Remix layout) and only touches quads matching TOD's exact invariant
(leaves other layouts alone). In place, so TOD's rasterized geometry is unchanged.
ASI 141312 @20:02:53.

Risks to watch on test:
- If the foliage IB is DEFAULT non-dynamic, `Lock(0)` fails (counter
  `g_billboardRemapLockFails`) and the remap is skipped -> no improvement; then need a
  copy-IB approach instead of in-place.
- Making Remix accept these as billboards could mis-treat real alpha CARD meshes
  (row 1, zwrite=1, 482-vert mesh) as camera-facing - watch foliage for new weirdness.
  If so, narrow the remap to zwrite-OFF unordered draws (row 2 type) only.

### RESULT: remap works but is NOT the cause - disabled

Verification run (remap then capture, summary at present 300):
```text
row=1  ... ABCACD=1   idx=1,0,2,1,2,3,...
row=2  ... ABCACD=1   idx=0,1,2,0,2,3,...
SUMMARY remapDraws=113  lockFails=0  captureRows=2
```
The remap definitively took effect (post-remap layout is A,B,C,A,C,D, 113 draws/run,
ZERO lock failures), but the user reports the displaced-foliage glitch is unchanged.
So the billboard index layout is NOT the cause (matches the "unsupported quad index"
warning never appearing in the active log). Both `kCaptureBillboardIndices` and
`kRemapBillboardIndices` set to false -> clean state. ASI 140800 @21:26:37.

### Glitch conclusion / ruled out

Conclusively ruled out as the cause: volumetric fog (pre-existed), the camera-resend
(disabling it made the distortion WORSE -> it stabilizes), the geometry generation
hash rule (removing `positions` crashes: "Position hash should never be empty"), and
the billboard quad index layout (remap verified, no effect).

Best remaining explanation: unstable motion vectors / denoiser ghosting from TOD's
CPU-animated foliage (software skinning + per-frame CPU billboard rebuild) being
re-hashed every frame (positions change -> new geometry hash -> no temporal history).
This is the rtx-remix #198 class and Remix won't let us change the position-based hash
(crash). Realistic remaining options: (a) DLSS Ray Reconstruction / denoiser settings
that handle disocclusion better; (b) `rtx.hideInstanceTextures` the distant-foliage
billboard texture (glitch gone, foliage reduced); (c) accept as an inherent
Remix<->TOD limitation. Not a config value we've found.

### 2026-06-04 ~22:40 - Frame gen tested; STANDING-STILL flicker confirms inherent

- DLSS Frame Generation was on (`rtx.dlfg.maxInterpolatedFrames = 3` in user.conf,
  `rtx.dlfg.enable` default true). Disabled it (`rtx.dlfg.enable = False`): the
  during-MOTION displacement reduced, but FPS dropped a lot (laggy) AND the user then
  noticed the trees FLICKER WHILE STANDING STILL. Frame extraction (v5) shows the palm
  fronds changing shape every frame with a static camera (the sun glow stays constant).
- Conclusion: frame gen only AMPLIFIED the motion case; the root is TOD CPU-animating
  the foliage (swaying) so its vertex data genuinely changes every frame -> Remix
  re-hashes it as new geometry each frame -> no temporal history -> flicker even when
  stationary. The `hashInstanceWithBoundingBoxHash=false` lever only applies when
  anti-culling is enabled (rtx_instance_manager.cpp:2265 gated by needsMeshBoundingBox)
  so it does not help by default. The clean fix (exclude positions from the geometry
  hash) crashes. There is no config/ASI fix that does not require the change Remix
  forbids -> INHERENT Remix<->TOD limitation for animated foliage.
- Re-enabled `rtx.dlfg.enable = True` (frame gen is not the root; restore FPS).
- Remaining real choices: (a) `rtx.hideInstanceTextures` the palm/tree texture(s) to
  remove the flickering foliage; (b) lower `rtx.dlfg.maxInterpolatedFrames` to ~1 to
  cut the motion-amplified case while keeping some FPS; (c) accept the flicker.

### 2026-06-04 ~23:30 - RE for an ASI fix: wind=rewind red herring; freeze-foliage path

User asked to RE for an ASI fix. Ghidra string scan found `WIND_controller`,
`setwindpause(truth)`, `setwindmode`, `windsize`, `wind_mill`, `grass`. BUT the
setwindpause/windsize/setwindmode commands are registered in TOD's Scene class
(`FUN_00899cc0`) right next to `rewindtimemultiplier`, `rewindresumetime`,
`flushrewind`, `ResetGame` -> they are the REWIND (time-travel) mechanic, NOT foliage
wind ("wind" = reWIND). The windsize getter `FUN_00896110` computes a time-delta, not
wind. There IS a separate real `WIND_controller` (string @009c5060, xref inside the
~120KB class-init `FUN_00719070`), but tracing it to a freezable per-frame global is a
long dig and may not be the sole driver of the foliage vertex animation.

Conclusion on the ASI path: the reliable fix that does NOT depend on finding the exact
animation source is to FREEZE the foliage geometry at the D3D9 level - detect the
animated alpha-tested quad foliage draws and pin their vertex buffer to a cached static
copy (like the managed-texture shadow subsystem, but for vertex buffers), so Remix sees
identical geometry every frame -> no re-hash -> no flicker. Trees become static (no
sway) but stable and ray-traced. Cost: substantial new ASI subsystem + careful
behavioral targeting so it doesn't freeze water/flags/muzzle/particle alpha geometry.
Open decision: build it, or take choice (a)/(c) above.

### Open risks / next:
- If the sun is STILL a bright disc after emissive=0, the source is the skybox sun
  texture itself and/or the sky environment probe (skyBrightness / skyProbeSide),
  not the sprite - then lower rtx.skyBrightness (a single global constant, still
  automatic - acceptable) to dim the sky-as-light without per-mission tuning.
- If the world is STILL dark with near-zero fog, volumetric is not the only cause;
  consider removing volumetric entirely (accept a less atmospheric sky) since
  "see enemies" is gameplay-critical, or add a clean global brightness/exposure.

## 2026-06-04 ~01:10 - World brighter ok; sun circle is a billboard sprite, not emissive

User: thinned fog helped (world brighter, wants a bit more); but the sun is STILL a
hard circle even with emissiveBlendOverrideEmissiveIntensity = 0.

Conclusion: emissive=0 ruling out the additive flare means the visible sun disc is a
sun BILLBOARD sprite (or the skybox sun), not the additive flare sprite. So emissive
/ bloom levers cannot remove it.

Changes (both rtx.conf):

```ini
rtx.vertexColorStrength = 1.3          ; was 1.0 - a bit lighter near+far (baked light)
rtx.hideInstanceTextures = 0x5EF9EBC260F4B6BC   ; hide the confirmed TOD sun-flare sprite
```

`0x5EF9EBC260F4B6BC` is the yellow flare sprite confirmed earlier in this doc
("Mission-2 Orange Sky Probe Follow-up": mat_5EF9EBC260F4B6BC, a small yellow flare
sprite near the sun). Hiding the instance removes the hard disc while leaving the
sky; it does NOT touch sky brightness, so the warm volumetric sky stays. Game hash
in Remix config is rule-compliant (like uiTextures / skyBoxTextures).

If the circle persists, 0x5EF9... is not the disc the user sees. Then IDENTIFY the
exact sun sprite by capture (Remix UI texture-under-reticle, or re-enable the ASI
texture discovery briefly at the sunset spot) and hide/soften that hash instead.
Avoid lowering rtx.skyBrightness as the first move - it would dim the now-good warm
sky globally rather than surgically removing the sun disc.

## 2026-06-05 - Animated alpha-world VB freeze test

Context:

- User accepted the current sky/haze result.
- Remaining issue: trees/foliage and some similar alpha structures flicker or
  appear unstable, especially around foliage silhouettes.
- Previous work ruled out camera resend, billboard index layout, DLFG as root,
  and the global Remix geometry-generation hash config (`positions` cannot be
  removed without crashing Remix).

Implemented ASI test:

- Added `kFreezeAnimatedAlphaWorldVertexBuffers = true`.
- Disabled `kEnableTodFogStateLog`; fog TSV logging was already diagnostic-only
  and was producing very large logs.
- The new path targets only large fixed-function indexed alpha world draws:
  - triangle-list indexed draw;
  - no RHW / no POSITIONT / no vertex shader;
  - alpha blend or alpha test enabled;
  - depth enabled and depth writes enabled;
  - primary render target;
  - non-render-target texture;
  - `primitiveCount >= 80`, `numVertices >= 64`.
- For a matching draw, the ASI copies only the used vertex range into a compact
  DEFAULT-pool frozen vertex buffer, caches it by original VB + stream range +
  index buffer range + texture pointer, temporarily binds the frozen VB for the
  draw, adjusts `BaseVertexIndex` for the compact range, then restores the
  original stream source immediately after the draw.

Reasoning:

- TOD appears to CPU-animate/sway foliage by changing vertex positions.
- Remix generation hashes require vertex positions, so the same animated tree
  becomes new geometry every frame and loses temporal history.
- Freezing the vertex range gives Remix stable geometry for those alpha world
  draws without touching textures, sky, camera, render targets, particles, or
  depth-disabled sun/glow overlays.
- Expected trade-off: foliage sway may become static; if flicker improves, this
  confirms the root and gives a usable mitigation.

Log:

```text
rtx-remix\logs\tod-foliage-freeze.tsv
```

Build:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 142848 bytes
scripts\TODCameraResend.asi timestamp: 2026-06-05 11:26:31
scripts\TODCameraResend.asi SHA256 1CDCF6C832BDF4DE730C86DEDD0E0E84D31BAB8FAACAB239898075A2D07560C7
tools\tod_camera_resend_asi\TODCameraResend.cpp SHA256 11029D07C5208D6CE5818E0FCD8A7072F47928968200E634C5A438B4834ABB6F
rtx.conf / scripts\rtx.conf SHA256 8223AECE0E8E986E09A29B4CD4F1FFD056F1A34414FA8687ECDA6A899E8FF987
```

Test expectation:

- If the flicker improves and trees stop changing shape, keep this path and
  tighten thresholds if any non-foliage object freezes.
- If foliage becomes visually wrong, lower the scope rather than widening it:
  likely require texture-hash review or TOD mesh-class RE.
- If `tod-foliage-freeze.tsv` has many `source_lock_failed` rows, the fallback
  is a copy/update path rather than direct VB readback.

## 2026-06-05 11:50 - Freeze test rejected; fixed sun injection disabled

User result from:

```text
C:\Users\utkar\Videos\NVIDIA\Total Overdose\Total Overdose 2026.06.05 - 11.38.58.01.mp4
```

- Foliage/structure flicker still visible.
- First mission warehouse showed floor glitches / black regions.

Video review:

- Extracted frames to `re_docs\floor_black_113858_frames`.
- Contact sheet: `re_docs\floor_black_113858_frames\contact.jpg`.
- Full late frame: `re_docs\floor_black_113858_frames\t08_full.png`.
- The warehouse artifact looks like hard black shadow/decal regions on the
  floor, not missing base floor geometry.

Freeze-log result:

```text
rtx-remix\logs\tod-foliage-freeze.tsv
created rows: 3
bound rows:   23
matched draw: primitiveCount=546, numVertices=482, stride=32
lock/create failures: 0
```

Conclusion:

- The VB-freeze experiment did run cleanly but did not fix the reported flicker.
- Keep the code retained but disabled; do not ship this path active.
- The indoor black floor regions are plausibly from the fixed ASI sun injection:
  it applied one directional light everywhere, including indoor mission areas,
  and could produce hard, wrong ray-traced shadows.

Changes:

```cpp
kFreezeAnimatedAlphaWorldVertexBuffers = false
kInjectSunLight = false
kEnableTodFogStateLog = false
```

Build:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
Built scripts\TODCameraResend.asi
scripts\TODCameraResend.asi size: 139776 bytes
scripts\TODCameraResend.asi timestamp: 2026-06-05 11:50:31
scripts\TODCameraResend.asi SHA256 A283120A9046510843AF45BDAD43DD3619C2285051BD9AFF1B6CC4FD0619B4B5
TODCameraResend.cpp SHA256 EDA17FB5913809011EF9A7777D5D1BB530AB4AA81711BCACA391589F31FDB3E6
```

Next test:

- Re-test the same first-mission warehouse and the foliage spot.
- If the black floor regions disappear, fixed sun injection was the source.
- If foliage still flickers, the next clean option is not VB freezing. Prefer a
  targeted foliage texture/hash review (`rtx.hideInstanceTextures`) or deeper TOD
  mesh-class RE to identify a narrower way to freeze only true vegetation.

## 2026-06-05 12:08 - Reverted local floor experiment; global flicker remains

User result:

- Disabling the ASI fixed sun injection did not resolve the mission-1 warehouse
  floor/black-region artifact.
- That artifact is local to mission 1 / that position.
- Flickering trees/structures remain visible across gameplay, so that is the
  next issue to isolate.

Active baseline:

```cpp
kInjectSunLight = true
kFreezeAnimatedAlphaWorldVertexBuffers = false
kEnableTodFogStateLog = false
```

Build:

```text
scripts\TODCameraResend.asi size: 139776 bytes, timestamp 2026-06-05 12:05:32
scripts\TODCameraResend.asi SHA256 9B9E2D70F0A1B0BE8B6558B5DCFBAAD2ABD27E32DDE85F9538E9252130C9EC6E
TODCameraResend.cpp SHA256 60B7B9D98D144C6DCB77598BB3CA0BB6330432F0212928EBD09795C5963B6013
```

Next direction:

- Mine the existing `tod-candidate-world` DDS dumps for real vegetation/alpha
  world hashes.
- Prefer config-level Remix classification/hide tests for confirmed foliage
  textures before adding any more runtime geometry mutation.
- If config cannot solve it cleanly, use Ghidra/TOD RE to find a real vegetation
  mesh/draw class and gate any hook by that class, not by broad draw heuristics.

## 2026-06-05 12:25 - Broad moving glitch scope correction

User clarified the artifact is not tree-specific. The visible issue is many
textures/objects glitching while the camera/player moves, so texture-specific
foliage hiding is not the right test.

State correction:

- No active `rtx.hideInstanceTextures` line.
- `kFreezeAnimatedAlphaWorldVertexBuffers = false`.
- ASI binary unchanged from the 12:05 baseline.

Global-motion isolation test:

```ini
rtx.dlfg.enable = False
```

applied to both active Remix config paths.

Reason:

- DLFG/interpolation is a global temporal feature and can warp unrelated moving
  objects/textures.
- This is reversible and does not add game-content-specific hacks.

If this does not change the symptom, stop treating it as a texture-list problem
and inspect transform timing / frame-to-frame instance identity / RT handoff.

## 2026-06-05 16:45 - Moving glitch video and binary follow-up

Video:

```text
C:\Users\utkar\Videos\NVIDIA\Total Overdose\Total Overdose 2026.06.05 - 16.34.22.01.mp4
```

Review artifacts:

```text
re_docs\moving_glitch_20260605_163422\contact.jpg
re_docs\moving_glitch_20260605_163422\dense_contact.jpg
re_docs\moving_glitch_20260605_163422\scene_003.jpg
```

Finding:

- The visible artifact is a previous-frame/feedback ghost during fast zipline
  camera motion, not a per-texture flicker.
- The ghosting affects the player and UI/text, so hiding foliage/world textures
  is the wrong class of fix.

Binary evidence:

- TOD's render-list interpreter (`FUN_004342c0`) contains full-screen texture and
  render-target commands.
- `FUN_0044e220` centralizes render-target and depth-stencil switching.
- This confirms the engine has a screen-space render-target/postprocess path,
  while Remix also has temporal denoiser/upscaler history.

Config state:

- DLFG-off test rejected and reverted:

```ini
rtx.dlfg.enable = True
rtx.dlfg.maxInterpolatedFrames = 1
```

- New active temporal-history test:

```ini
rtx.enableRayReconstruction = False
```

Reason:

- `rtx.postfx.enable = False` already disables Remix postfx motion blur.
- Disabling DLFG disabled generated frames, not the denoiser/RR history.
- Ray Reconstruction is the narrowest next test for global ghosting before
  changing ASI render-target routing.

If unchanged:

- Do not add texture hashes.
- Test ASI RT routing next: narrow the current all-color-RT redirect/composite
  skip so TOD's 1280x720 feedback/postprocess chain is not broadly folded into
  the primary scene path.

## 2026-06-05 17:02 - Moving ghosting: active ASI render-target routing test

User result:

- `rtx.enableRayReconstruction = False` did not fix the moving glitch.
- Therefore the RR-off config test was reverted.

Active config state:

```ini
rtx.dlfg.enable = True
rtx.dlfg.maxInterpolatedFrames = 1
rtx.postfx.enable = False
```

Active ASI state:

```cpp
constexpr bool kRedirectFullSizeSceneRtToPrimary = true;
constexpr bool kSkipRtTextureCompositesToPrimary = true;
constexpr bool kRedirectAllColorRtTexturesToPrimary = false;
constexpr bool kSkipAllRtTextureComposites = false;
```

Reason:

- The symptom in the 16:34 zipline video is global feedback/ghosting; it moves
  across player, scene, and UI/text, so it is not a tree/material/hash issue.
- Binary RE already shows TOD uses full-screen texture and render-target
  commands inside the render-list interpreter.
- The previous ASI behavior was broad enough to disrupt offscreen RT ping-pong:
  it redirected all color RT textures to primary and skipped all RT texture
  composites.
- The new test keeps the full-size scene RT workaround but allows ordinary
  offscreen render-target composites to execute.

Build state:

```text
scripts\TODCameraResend.asi size 140288, timestamp 2026-06-05 17:02:01
scripts\TODCameraResend.asi SHA256 27ACF969EF7AD5CFA926B9C328526DAA980290CB072D35907C1B363D8783DB1D
tools\tod_camera_resend_asi\TODCameraResend.cpp SHA256 215F15FDFBE37CBB45A7ADD1EB03615CB54382A301FF9AFB3A6FA14F432AB545
rtx.conf SHA256 8223AECE0E8E986E09A29B4CD4F1FFD056F1A34414FA8687ECDA6A899E8FF987
scripts\rtx.conf SHA256 2D61CDE8303E4CFB0762A80F1935392104A67ACF8F8C65BC52C4E4769015BB81
```

## 2026-06-05 17:48 - Moving glitch: Remix instance-stability diagnostic

User result:

- The world-transform resend test did not fix the moving glitch.

Reverted:

- Removed `D3DTS_WORLD` resend from the ASI.
- Rebuilt the ASI with the previous camera-only resend source.

Active diagnostic config:

```ini
rtx.enableAlwaysCalculateAABB = True
rtx.useBuffersDirectly = False
```

Rationale:

- The glitch remains a global moving-geometry / unstable-instance symptom.
- Remix source documents `rtx.enableAlwaysCalculateAABB` as an instance tracking
  aid for skinned and vertex-shaded calls.
- Remix source documents `rtx.useBuffersDirectly` as direct use of incoming
  vertex buffers. Disabling it is a clean diagnostic for stale/shared dynamic
  D3D9 vertex-buffer reads.
- This is still generic Remix/D3D9 behavior, not a content-specific texture hack.

Build state:

```text
scripts\TODCameraResend.asi size 139776, timestamp 2026-06-05 17:47:29
scripts\TODCameraResend.asi SHA256 80EA0C88C16DABB1911C87034BC6D69DC37891F11626FE6C53B5077B5FD50AA3
tools\tod_camera_resend_asi\TODCameraResend.cpp SHA256 60B7B9D98D144C6DCB77598BB3CA0BB6330432F0212928EBD09795C5963B6013
rtx.conf SHA256 35BBF7271DF3DDEA999CAB8C4EBB1763221C2101B66A38E17BBADBA6EF9FA6A9
scripts\rtx.conf SHA256 0D7808AF6A4C63F10B9C03323ED2FC70745033A4CF591C584A1220E0E95DC8D8
```

## 2026-06-05 17:10 - Moving ghosting: RT routing rejected, temporal history test

User result:

- Narrowing ASI render-target routing did not fix the moving ghosting.

Restored ASI state:

```cpp
constexpr bool kRedirectFullSizeSceneRtToPrimary = true;
constexpr bool kSkipRtTextureCompositesToPrimary = true;
constexpr bool kRedirectAllColorRtTexturesToPrimary = true;
constexpr bool kSkipAllRtTextureComposites = true;
```

Active diagnostic config:

```ini
rtx.upscalerType = 0
rtx.useDenoiser = False
```

Reason:

- This symptom is still global temporal feedback, not texture identity.
- Since DLFG-off, RR-off, and narrowed ASI RT routing did not fix it, the next
  clean isolation step is disabling DLSS upscaling and the path-tracing denoiser.
- If this improves the smear, split these two settings next; if not, instrument
  TOD's render-target command stream around the zipline scene.

Build state:

```text
scripts\TODCameraResend.asi size 139776, timestamp 2026-06-05 17:09:43
scripts\TODCameraResend.asi SHA256 D18C8697EDDE636676246DFA614EAB897C59D18D4091A30A12405F84075F6827
tools\tod_camera_resend_asi\TODCameraResend.cpp SHA256 60B7B9D98D144C6DCB77598BB3CA0BB6330432F0212928EBD09795C5963B6013
rtx.conf SHA256 BBC1C4422CE1AFB8F44A0F22B4A7603A39592A4057CED36860B34AD5B22D57C0
scripts\rtx.conf SHA256 B3596F76FFBCB403EDF002771E413632CD05B3C2C8983ACAF1053BCCBA253347
```

## 2026-06-05 17:30 - Moving glitch: world transform resend test

User result:

- The external tree/box issue was caused by texture upscaling outside this ASI
  test.
- The real moving smear/glitch still exists.

Reverted diagnostic config:

```ini
rtx.upscalerType = 0
rtx.useDenoiser = False
```

Binary evidence:

- `FUN_0044def0`: submits `SetTransform(D3DTS_WORLD = 0x100, matrix)` and
  mirrors that matrix into TOD renderer state.
- `FUN_0044e400`: submits `D3DTS_VIEW`.
- `FUN_0044e580`: submits `D3DTS_PROJECTION`.
- `FUN_004540e0`: fixed-function indexed mesh draw path
  (`SetStreamSource`, `SetFVF`, `SetIndices`, `FUN_0044f8a0`, then
  `DrawIndexedPrimitive`).
- `FUN_00421530` and `FUN_004342c0` show TOD uses command-list state sequencing:
  transforms can be set earlier than the specific mesh draw Remix sees.

ASI change:

- Cache `D3DTS_WORLD` in the transform hook.
- Resend `D3DTS_WORLD`, `D3DTS_VIEW`, and `D3DTS_PROJECTION` before
  fixed-function indexed world draws.
- Keep the existing guards for pretransformed UI, shader draws, and identity-view
  overlay passes.

Rationale:

- This is a proper D3D9-state synchronization fix, not a per-texture workaround.
- The symptom looks like unstable object transforms / instance reconstruction
  during movement. Resending world state gives Remix the same complete transform
  tuple that D3D9 already has at draw time.

Build state:

```text
scripts\TODCameraResend.asi size 140288, timestamp 2026-06-05 17:29:48
scripts\TODCameraResend.asi SHA256 FC968F9FFFB559F6A7449E45A9033B3887B68464CB4A95DD67504C00D759B07A
tools\tod_camera_resend_asi\TODCameraResend.cpp SHA256 648FF2ED593B4A03DDF853579AF05BD5A31B294FFA3FC6741561D9A926913042
rtx.conf SHA256 8223AECE0E8E986E09A29B4CD4F1FFD056F1A34414FA8687ECDA6A899E8FF987
scripts\rtx.conf SHA256 2D61CDE8303E4CFB0762A80F1935392104A67ACF8F8C65BC52C4E4769015BB81
```
