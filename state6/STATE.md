# State 6 - Ray-traced sun light + proper volumetric haze

Saved: 2026-06-04

## Summary

- **Sun**: TOD has no real sun (lighting baked into vertex colors; on-screen sun is a
  2D billboard). The ASI now injects a D3D9 directional light each frame
  (`InjectSunLight` in `Hook_Present`, `SetLight` + `LightEnable`), which Remix
  ray-traces as a real distant sun (confirmed in dxvk-remix source: `SetLight` ->
  `DirtyLights` -> `addLights`, independent of `D3DRS_LIGHTING`). User: "the sun looks
  gooood."
- **Fog/haze**: switched from the legacy game-fog REMAP (which fogged the whole sky -
  sky at infinite distance accumulates the full fog color) to the PROPER base
  volumetric with manual params. The froxel grid only reaches `froxelMaxDistanceMeters`,
  so the WORLD gets a light dusty haze while the SKY keeps its real skybox color.
- **Sky**: shows TOD's actual per-mission skybox (m1 blue, m2 desert). The previous
  green/orange skies were fog tint only; sky-color-from-fog is an accepted tradeoff
  (user: "Ig the sky is an acceptable tradeoff").
- **Brightness**: world lit by TOD's full baked vertex lighting
  (`rtx.vertexColorStrength = 1.3`); auto-exposure off (it washed the sky).
- **Render distance**: `kTodCameraFarClipOverride` raised to 8000 (was 3000).
- Root and scripts `rtx.conf` are byte-identical.

## Active ASI

- `scripts/TODCameraResend.asi` size: 140288 bytes
- `scripts/TODCameraResend.asi` timestamp: 2026-06-04 01:12:01
- Source: `tools/tod_camera_resend_asi/TODCameraResend.cpp`
- Build: `cmd /c tools\tod_camera_resend_asi\build.bat` (game + NvRemixBridge closed)

## Config SHA256

- root/rtx.conf:    A696C359110D12879F7EB3919F1E3B83B495A3F8F19D24B788E4C7DFA2C5176C
- scripts/rtx.conf: A696C359110D12879F7EB3919F1E3B83B495A3F8F19D24B788E4C7DFA2C5176C

## Key config

```ini
rtx.vertexColorStrength = 1.3
rtx.autoExposure.enabled = False

# Fog/haze: base volumetric, NOT the legacy fog remap
rtx.enableFog = False
rtx.volumetrics.enableFogRemap = False
rtx.volumetrics.enableFogColorRemap = False
rtx.volumetrics.enable = True
rtx.volumetrics.transmittanceColor = 0.94, 0.92, 0.87
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 800
rtx.volumetrics.froxelMaxDistanceMeters = 3000
```

## Key ASI constants (TODCameraResend.cpp)

```text
kInjectSunLight = true            ; directional sun, index 0, dir (0,-0.8,0.6), warm (2.0,1.8,1.5)
kTodCameraFarClipOverride = 8000  ; render distance (was 3000)
kMarkTodSkyDrawsWithViewportMinZ  ; SkyBox MinZ marking (skyMinZThreshold path)
```

## Open / next (not blocking)

- Sun light direction is a fixed test vector - not yet wired to TOD's real
  sun-direction vector (step 2: RE the sun direction for a per-mission dynamic sun).
- The 2D sun billboard sprite is still drawn (un-hidden).
- Per-mission sky color is the skybox only (the volumetric haze is a single global
  color); accepted tradeoff for a correct, un-washed sky.

## Evidence

- re_docs/tod_render_distance_texture_handling_re_20260603.md (sun + fog method)
- re_docs/rtx_remix_runtime_debug_log.md
- re_docs/rtx_remix_re_findings.md
