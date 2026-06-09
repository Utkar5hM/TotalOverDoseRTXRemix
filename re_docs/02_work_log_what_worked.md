# Work Log: What Worked

Purpose: concise decision log. This removes launch/test instructions and keeps only the engineering outcome. Full chronological notes are archived under [archive/source_notes](archive/source_notes).

## Current Baseline

Checked from local files on 2026-06-08:

- Active shim: `scripts/TODRemixShim.asi`.
- Active shim source: `tools/tod_remix_shim/TODRemixShim.cpp`.
- Active shim INI: `scripts/TODRemixShim.ini`.
- Active Remix config: `scripts/rtx.conf`.
- Active DXVK config: `scripts/dxvk.conf`.
- Root `rtx.conf`, `dxvk.conf`, and `user.conf` are not present in this package root.

Key active config/source facts:

```ini
rtx.enableRaytracing = True
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
rtx.captureInstances = True
rtx.uniqueObjectDistance = 25.0
rtx.raytracedRenderTarget.enable = True
rtx.skyBrightness = 1.5
rtx.skyAutoDetect = 0
rtx.skyMinZThreshold = 0.99
rtx.enableFog = False
rtx.volumetrics.enable = True
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 180
# Remix default local tonemapper remains active; no rtx.tonemappingMode override.
rtx.localtonemap.shadows = 1.4
d3d9.evictManagedOnUnlock = True
```

```cpp
kPatchTodCameraFarClip = true
kMarkTodSkyDrawsWithViewportMinZ = true
kEnableCameraResend = true
kInjectSunLight = false
kEnableSunAutoAim = false
kFreezeAnimatedAlphaWorldVertexBuffers = false
kEnableTodFogStateLog = false
```

`scripts/TODRemixShim.ini` has asset capture and crawler disabled by default:

```ini
[AssetCapture]
Enabled=0

[AssetCrawler]
Enabled=0
```

## Worked

- Ghidra-backed renderer mapping worked. TOD is confirmed as fixed-function D3D9 with real indexed 3D geometry, explicit world/view/projection transforms, and standard vertex/index buffers.
- Camera resend and render-target support belong in the shim. The game can skip resubmitting view state; the shim's D3D9 state support is a valid compatibility layer.
- Restoring `rtx.raytracedRenderTarget.enable = True` and the nine render-target hashes was required for mission 3 raytracing. The ASI RT redirect did not replace Remix's native raytraced-render-target feature.
- Keeping managed-texture DEFAULT-copy behavior at `SetTexture` time was safer than later draw-time/stage-0-only substitution. The later rewrite broke mission 3.
- `d3d9.evictManagedOnUnlock = True` remains part of the working managed-texture/hash baseline.
- UI/HUD is a config tagging problem. RHW menu/HUD draws need confirmed hashes in `rtx.uiTextures`; hardcoding UI hashes in the ASI was avoided.
- Sky is solved through TOD's actual SkyBox draw path. The shim marks SkyBox draws by temporary viewport `MinZ`; Remix consumes that through `rtx.skyMinZThreshold = 0.99`.
- TOD's atmosphere is real fixed-function fog, but the current package uses volumetric/tonemapping tuning with fixed-function fog disabled.
- Moving displacement was isolated to Remix instance temporal matching. `rtx.uniqueObjectDistance = 25.0` is the best confirmed compromise.
- Per-map asset capture works through TOD-owned resource paths. The automated crawler completed all 114 registered live AssetBlocks and produced a 3,554-texture capture union.
- Current 2026-06-08 lighting baseline is the skybox-shadow/local-tonemap branch. Injected sun and auto-aim code remain in source, but are compiled off in this package to avoid double/opposite shadows.

## Rejected

- `rtx.enableNearPlaneOverride = True`: no useful black-screen improvement.
- `d3d9.shaderModel = 0`: parsed, no meaningful improvement.
- Blindly stacking `rtx.raytracedRenderTargetTextures`: made early raytraced path worse until the known-good nine-hash set was restored from the working build.
- Broad managed-A8 world copying: caused sky/world material corruption and texture mixups.
- Runtime texture hashing, DDS dumping, candidate discovery, or UI refresh in the shipping ASI: too invasive and caused regressions in mission 3.
- Draw-time texture substitution and stage-0-only substitution: regressed mission 3.
- `rtx.skyAutoDetect = 2`: replaced by explicit SkyBox runtime marker.
- Static `rtx.skyBoxTextures` as the final sky solution: useful evidence but incomplete/blunt across missions.
- Billboard index remap for water/spikes: remap worked mechanically but was not the root cause.
- Foliage/alpha vertex-buffer freeze: ran without lock/create failures, but did not fix the reported global moving glitch and caused unrelated risk.
- Disabling fixed sun injection was useful in older branches, but this package already has sun injection compiled off; future floor/shadow debugging should compare skybox/environment state first.
- `rtx.dlfg.enable = False`: reduced some motion amplification but did not fix the root moving glitch and hurt performance.
- `rtx.enableRayReconstruction = False`: did not fix the moving glitch.
- Narrowing ASI render-target routing: did not fix the moving ghosting.
- Disabling upscaler/denoiser (`rtx.upscalerType = 0`, `rtx.useDenoiser = False`): rejected after the relevant artifact remained.
- Resending `D3DTS_WORLD` before draws: not a fix; reverted to camera-only resend.
- `rtx.enableAlwaysCalculateAABB = True` plus `rtx.useBuffersDirectly = False`: no fix.
- `d3d9.allowDiscard = False`: no fix.
- `rtx.enableInstanceDebuggingTools = True`: proved the instance-matching root cause, but caused broad temporal blur and is not a final setting.
- `rtx.uniqueObjectDistance = 75.0`: still too permissive.
- `rtx.uniqueObjectDistance = 15.0`: less wrong-instance reuse, but visible player/car blur.
- `rtx.uniqueObjectDistance = 20.0`: tested as compromise; user restored `25.0`.

## Do Not Repeat Without New Evidence

- Do not assume black RenderDoc captures mean TOD lacks D3D9 indexed geometry. They captured a black/white fullscreen present path, not the useful world draw stream.
- Do not solve render distance by changing texture/LOD capture knobs. Far clip is camera/projection/frustum state; LOD and texture residency are separate.
- Do not force-load blocks through incomplete archive readers. Use TOD's scene/resource-controller path or controlled gameplay traversal.
- Do not put content hashes in C++ unless they are read from external config. Keep hashes in `rtx.conf` or `TODRemixShim.ini`.
- Do not widen A8 handling globally just to make one missing texture appear. Find the material/render-state class first.
- Do not treat every moving artifact as a texture issue. The global motion displacement was instance matching.
- Do not treat older `TODCameraResend` file names as active runtime truth. Current package uses `TODRemixShim`.

## Still Risky / Open

- Minor flicker can remain with `rtx.uniqueObjectDistance = 25.0`; lower values trade it for player/car blur.
- Asset crawler does not cover cutscenes while `IncludeCutscenes=0`, nor mission-script-only procedural variants that appear only after gameplay events.
- Current lighting relies on Remix skybox/environment shadows with local tonemapping (`rtx.localtonemap.shadows=1.4`, `rtx.skyBrightness=1.5`, transmittance distance `180`). The `[Sun]` INI block and sprite hash are reference values only unless the ASI is rebuilt with `kInjectSunLight=true` and `kEnableSunAutoAim=true`.
- Local mission-specific floor/shadow artifacts may still need targeted analysis. The broad sun-disable experiment did not solve the warehouse floor case.
- Some old archived notes point to `tools/tod_camera_resend_asi/` and `scripts/TODCameraResend.asi`; those were historical names/paths.

## Source Notes

Use these only when a full audit trail or exact table is needed:

- [archive/source_notes/rtx_remix_runtime_debug_log.md](archive/source_notes/rtx_remix_runtime_debug_log.md) - chronological runtime/debug log.
- [archive/source_notes/tod_render_distance_texture_handling_re_20260603.md](archive/source_notes/tod_render_distance_texture_handling_re_20260603.md) - render distance, sky/fog, material handling, moving glitch history.
- [archive/source_notes/rtx_remix_re_findings.md](archive/source_notes/rtx_remix_re_findings.md) - first Ghidra renderer pass.
- [archive/source_notes/rtx_remix_re_pass2_model_path.md](archive/source_notes/rtx_remix_re_pass2_model_path.md) - Model render path and opcode pass.
- [archive/source_notes/tod_per_map_asset_capture_20260607.md](archive/source_notes/tod_per_map_asset_capture_20260607.md) - asset capture/crawler details.
- [archive/source_notes/rtx_remix_candidate_report.md](archive/source_notes/rtx_remix_candidate_report.md) - full candidate hash tables.

