# RTX Remix Runtime Debug Log

Date: 2026-06-01

Game: Total Overdose (`TOD.exe`)

Runtime: RTX Remix `remix-1.4.2+cfb6edc9`

## Current Baseline

`rtx.conf` is intentionally minimal:

```ini
rtx.enableRaytracing = False
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
```

`dxvk.conf` is intentionally neutral:

```ini
# No DXVK compatibility overrides are active.
```

`d3d9.allowDiscard = False` was tested for the black main-menu thumbnail issue. It produced no visible improvement and was removed from the active baseline.

## Stable Observations

- The main menu has been black from the beginning, including before later raytracing diagnostics.
- With ray tracing disabled and post effects disabled, gameplay can show a very dark/green partial scene.
- Cutscene/video-like content can display correctly and colorfully. This proves present/swapchain output is not globally broken.
- The RTX Remix overlay itself renders correctly.
- The gameplay/camera path remains broken: raytracing on consistently gives a fully black screen.

## Recurring Log Signals

The main repeated Remix/DXVK clues are:

```text
[RTX-Compatibility-Info] Trying to raytrace but not detecting a valid camera.
Attempted invert a non-invertible matrix.
[RTX-Compatibility-Info] Texture 0 without valid hash detected, skipping drawcall.
[RTX-Compatibility-Info] Found a draw call to a non-primary, non-raytraced render target.
```

Other recurring but probably secondary warnings:

```text
D3D9DeviceEx::SetRenderState: Unhandled render state D3DRS_DITHERENABLE
D3D9DeviceEx::SetRenderState: Unhandled render state D3DRS_LASTPIXEL
D3D9Texture2D::QueryInterface: Unknown interface query
794950f2-adfc-458a-905e-10a10b0b503b
```

## Tests Already Tried

### Near Plane Override

Tested:

```ini
rtx.enableNearPlaneOverride = True
```

Result: no evidence it helps. Removed from baseline.

Conclusion: do not use this for current black-screen debugging. It is more relevant later for viewmodel/near-plane clipping once camera reconstruction works.

### Raytracing Disabled Baseline

Tested:

```ini
rtx.enableRaytracing = False
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
```

Result: best current baseline. Main menu is still black, but gameplay can show a dim/green partial 3D scene.

Conclusion: keep this as the recovery baseline after failed tests.

### Forced Raytracing / Fallback Lighting

Tested:

```ini
rtx.enableRaytracing = True
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
rtx.sceneKeepAliveFrames = 300
rtx.vertexColorStrength = 0.0
rtx.ignoreAllVertexColorBakedLighting = True
rtx.fallbackLightMode = 2
rtx.fallbackLightType = 0
rtx.fallbackLightRadiance = 6.0, 6.0, 6.0
rtx.fallbackLightDirection = -0.3, -1.0, 0.4
```

Result: black/white flat output, not a useful scene.

Conclusion: lighting is not the first blocker. The valid-camera failure prevents fallback light from helping.

### Fixed Function / Shader Model Override

Tested:

```ini
d3d9.shaderModel = 0
```

Result: setting parsed, no meaningful improvement.

Conclusion: do not keep this in baseline.

### Camera Keepalive / Unknown Camera Objects

Tested:

```ini
rtx.fusedWorldViewMode = 1
rtx.sceneKeepAliveFrames = 600
rtx.skipObjectsWithUnknownCamera = False
```

Result: parsed, no visible improvement.

Conclusion: not sufficient for this game in current state.

### Raytraced Render Target Hashes

Manually tested render-target hash:

```ini
rtx.raytracedRenderTarget.enable = True
rtx.raytracedRenderTargetTextures = 0x4E44041F9E27548E
```

Result: parsed correctly but produced full black. Log still reported no valid camera / skipped texture / non-primary render target.

Remix UI-saved render-target hash:

```ini
rtx.raytracedRenderTarget.enable = True
rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB
```

Result: parsed correctly but produced full black.

Conclusion: do not continue stacking render-target hashes until camera reconstruction is better understood. This made the raytraced path worse, not better.

Candidate render-target hashes observed in screenshots:

```text
F885C65160789A8B
B566CD7C690A3630
4E44041F9E27548B
261F0A146E18E6FA
572A88A0A2E43086
609466F92E702865
D824D823D9CCF030
1E604227861E0BCB
```

### World Matrix / Vertex Capture

Tested:

```ini
rtx.enableRaytracing = True
rtx.useWorldMatricesForShaders = False
rtx.useVertexCapture = True
rtx.useVertexCapturedNormals = True
```

Result: parsed, still full black. Same valid-camera failure.

Conclusion: do not repeat as-is.

### Coordinate Handedness

Tested:

```ini
rtx.enableRaytracing = True
rtx.leftHandedCoordinateSystem = True
```

Result: parsed, still full black. Same valid-camera failure.

Conclusion: do not repeat as-is.

### Discarded Texture Preservation

Tested:

```ini
d3d9.allowDiscard = False
```

Result: parsed. No visible improvement for black main menu thumbnails.

Conclusion: weak/no effect so far. Can be removed if it causes regressions.

## Main Menu Notes

Screenshot `screenshots/9.png` shows:

- Game area is black.
- RTX Remix overlay text and developer menu render correctly.
- Game Setup texture thumbnails are mostly black.
- White, magenta/yellow checker, grid, and font-sheet textures are visible but not the missing menu.

Interpretation: there is currently no useful menu/logo/button texture exposed for UI tagging. The menu may be drawn through a dynamic/composite path that Remix is not capturing as useful texture thumbnails.

## Cutscene Notes

Screenshots under `screenshots/r` show a later cutscene/video-like frame that displays normally with color.

Interpretation: this is likely a video/fullscreen texture path. It proves presentation is working, but it does not prove the 3D gameplay camera path is reconstructable.

## RE / RenderDoc Context

Prior RE notes remain important:

- `re_docs/rtx_remix_re_findings.md` shows the engine does submit real D3D9 indexed geometry with XYZ/normal/UV formats and world transforms.
- Follow-up Ghidra MCP inspection found only six `IDirect3DDevice9::SetTransform` vtable call sites (`+0xb0`) in `TOD.exe`:
  - `FUN_0044def0` at `0044df03`: `SetTransform(D3DTS_WORLD = 0x100, matrix)`.
  - `FUN_0044e400` at `0044e548`: `SetTransform(D3DTS_VIEW = 2, matrix)`.
  - `FUN_0044e580` at `0044e667`: `SetTransform(D3DTS_PROJECTION = 3, matrix)`.
  - `FUN_0044d2e0`, `FUN_0044d5d0`, and `FUN_0044ee70`: texture transform calls (`0x10` / `0x11`), not camera transforms.
- `FUN_00421530` is the main render/camera setup loop: it sets projection via `FUN_0044e580`, conditionally sets view via `FUN_0044e400` when the camera matrix changes, then submits render command lists through `FUN_004342c0`.
- `re_docs/renderdoc_black_screen_analysis.md` shows captured RenderDoc frames are genuinely black/white in the D3D9 primary/final target and mostly contain fullscreen present-style draws, not useful indexed world draws.

Current combined interpretation:

The binary has a hookable 3D path and explicit D3D9 world/view/projection transform submission. In principle, this is the class of state Remix normally needs. The runtime still reports no valid camera, so the failure is likely about timing, render-target pass selection, matrix validity/invertibility, or Remix's reconstruction heuristics rather than total absence of D3D9 camera state.

## Do Not Repeat Without New Evidence

- Do not re-add `rtx.enableNearPlaneOverride = True` for the current black-screen issue.
- Do not turn on raytracing without expecting full black unless testing a specific new camera/render-target theory.
- Do not keep adding render-target hashes blindly; two parsed hashes already made the raytraced path fully black.
- Do not use `d3d9.shaderModel = 0` as baseline; it parsed but did not help.
- Do not tag white/checker/font-sheet textures as the missing menu.

## Next Useful Directions

1. Investigate why Remix cannot detect a valid camera despite the game submitting world transforms.
2. Look for game-side projection/view `SetTransform` calls in RE notes or Ghidra, not just world transforms.
3. Capture a RenderDoc frame during the dim-visible gameplay state, not the black menu or final black present, and verify whether D3D9 view/projection state exists.
4. If menu remains black, look for a non-texture UI/composite path rather than ordinary UI texture tagging.

Update after Ghidra MCP pass:

- View/projection calls do exist. Next RE target is no longer "find whether view/projection exist"; it is "understand whether they are submitted before the specific draws Remix sees as raytrace candidates, and whether the view matrix produced by `FUN_004687d0` / `FUN_004684e0` is valid/invertible for those passes."
- A plausible game-specific workaround to investigate is forcing `SetTransform(D3DTS_VIEW)` and `SetTransform(D3DTS_PROJECTION)` to be resent every frame/pass before world draw command lists, in case Remix misses state that the game relies on D3D9 to preserve.

## Internet Research Notes

Date: 2026-06-01

Sources checked:

- NVIDIA RTX Remix FAQ: https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-0/docs/remix-faq.html
- NVIDIA RTX Remix Game Setup docs: https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/runtimeinterface/remix-runtimeinterface-gamesetup.html
- NVIDIA RTX Remix runtime setup docs: https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.3.6-2/docs/gettingstarted/learning-runtimesetup.html
- NVIDIA RTX Remix compatibility wiki: https://github.com/NVIDIAGameWorks/rtx-remix/wiki/Compatibility
- RTX Remix RtxOptions reference: https://raw.githubusercontent.com/NVIDIAGameWorks/dxvk-remix/main/RtxOptions.md
- RTX Remix releases: https://github.com/NVIDIAGameWorks/rtx-remix/releases
- ModDB Total Overdose RTX Remix compatibility: https://www.moddb.com/games/total-overdose-a-gunslingers-tale-in-mexico

Findings:

- Total Overdose is listed on ModDB as RTX Remix `Not Compatible`, with no known runtime version, config file, captures, assets, or mods.
- Current installed Remix runtime `remix-1.4.2+cfb6edc9` matches the latest public release series found on GitHub: RTX Remix 1.4.2, released 2026-04-21.
- NVIDIA documentation says Remix works best with DirectX 8/9 fixed-function style rendering. Shader-heavy or unusual render paths may not expose standardized camera/scene/material data, which is exactly consistent with the local `no valid camera` logs.
- NVIDIA's Game Setup docs confirm UI tagging only helps when actual UI textures are visible to Remix. In `screenshots/9.png`, the menu thumbnails are mostly black, so there is no useful UI texture to tag yet.
- The RtxOptions reference confirms several settings we already tested are real options: `rtx.fusedWorldViewMode`, `rtx.useVertexCapture`, `rtx.useWorldMatricesForShaders`, `rtx.leftHandedCoordinateSystem`, and `rtx.raytracedRenderTargetTextures`.
- The `rtx.raytracedRenderTargetTextures` option is intended for render-target descriptor hashes, but local tests with parsed hashes still produced full black and did not resolve the valid-camera failure.

Recommended approach from research:

1. Stop treating this as a normal config-tuning problem. External compatibility data already says the game is not compatible, and local logs match a deep camera/reconstruction failure.
2. Use Remix Debug View -> Geometry Hash in dim-visible gameplay to verify whether stable geometry exists in the runtime. NVIDIA recommends Geometry Hash as the check for whether game content is actually being processed by Remix.
3. Reverse-engineer the game camera path next: find `SetTransform(D3DTS_VIEW)` and `SetTransform(D3DTS_PROJECTION)`, not only `D3DTS_WORLD`.
4. Capture a known dim-visible gameplay moment and inspect whether view/projection transforms are valid and invertible.
5. If camera transforms are absent or fused into shader constants, the realistic path is game-specific runtime work or a game-side/wrapper patch, not more texture tagging.
6. Consider filing a Remix runtime issue only after collecting a minimal package: logs, screenshots, `rtx.conf`, `dxvk.conf`, RenderDoc/Remix capture if possible, and RE notes showing real D3D9 indexed geometry plus missing/invalid camera.

## 2026-06-01 - Camera Resend ASI Implementation

Added a reversible diagnostic ASI hook to test the camera-state timing theory.

Files:

- Source: `tools/tod_camera_resend_asi/TODCameraResend.cpp`
- Build script: `tools/tod_camera_resend_asi/build.bat`
- Expected output: `scripts/TODCameraResend.asi`
- Runtime log: `rtx-remix/logs/tod-camera-resend.log`

The hook:

- Probes `Direct3DCreate9` from the loaded `d3d9.dll`.
- Hooks shared `IDirect3D9::CreateDevice` vtable slot 16.
- Hooks the returned `IDirect3DDevice9` vtable slots:
  - `SetTransform`, index 44 / offset `0xB0`.
  - `DrawPrimitive`, index 81 / offset `0x144`.
  - `DrawIndexedPrimitive`, index 82 / offset `0x148`.
- Caches the latest `D3DTS_VIEW` and `D3DTS_PROJECTION`.
- Re-submits cached view/projection immediately before each draw call.

This should show whether Remix's `not detecting a valid camera` message is caused
by camera state being unavailable at the exact draw calls Remix wants to raytrace.

Smoke test result:

- The ASI loaded and hooked `IDirect3D9::CreateDevice`.
- The returned `IDirect3DDevice9` hooks installed successfully.
- It observed both `D3DTS_VIEW` and `D3DTS_PROJECTION`.
- It resent camera state before more than 80,000 draw calls in one short run.
- The game repeatedly submits identity `D3DTS_VIEW` matrices between real camera
  matrices, likely for UI/menu/fullscreen passes.

Follow-up hardening:

- The hook now ignores identity `D3DTS_VIEW` matrices for the cached camera used by
  the resend path.
- The resend path now runs only before `DrawIndexedPrimitive`, not `DrawPrimitive`,
  to reduce interference with fullscreen/UI draw paths.

Current next test config:

```ini
rtx.enableRaytracing = True
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
```

Result from that test:

- Screen remained black.
- ASI-side log proved the hook was active and re-sent camera state before more
  than 600,000 indexed draws.
- Latest Remix-side log moved under `scripts/rtx-remix/logs/` because relative log
  paths changed during ASI/loader startup.
- Remix still reported:
  - `Skipped drawcall, using pre-transformed vertices which isn't currently supported.`
  - `Trying to raytrace but not detecting a valid camera.`
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target.`

Interpretation: simply resending fixed-function view/projection before indexed
draws is not sufficient. Next ASI revision logs draw-layout state (`FVF`,
`XYZRHW`, vertex declarations with `POSITIONT`, and vertex shader usage) so we can
separate real fixed-function world geometry from unsupported pre-transformed/UI
draws.

Implementation note: diagnostic ASI v2 is built and installed as
`scripts/TODCameraResend.asi`. It writes to the root
`rtx-remix/logs/tod-camera-resend.log` using an absolute path derived from the ASI
module location, so logs should no longer split between root and `scripts/`.

Result from ASI v2 gameplay run:

- Gameplay was reached by the user.
- ASI summary at present 1020:
  - `indexed=407969`
  - `fixedIndexed=407969`
  - `preTIndexed=0`
  - `shaderIndexed=0`
  - `unknownIndexed=0`
  - `resends=407969`
  - `primitive=97745`
  - `preTPrimitive=57229`
- This proves the gameplay/world path contains many normal fixed-function indexed
  draws with no vertex shader and no pre-transformed indexed vertices.
- The pre-transformed warning appears to come from early `DrawPrimitive` fullscreen
  or menu/UI-style draws (`FVF 0x144` / `0x44`, `XYZRHW`), not from the indexed
  world geometry.
- Latest Remix log still reports early:
  - `Skipped drawcall, using pre-transformed vertices which isn't currently supported.`
  - `Trying to raytrace but not detecting a valid camera.`
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target.`
- The server-side Remix logs still land in `scripts/rtx-remix/logs/`, and
  `remix-dxvk.log` says `Automatic Graphics Preset in use`, so `user.conf` is not
  reliably loaded by the 64-bit server path.

Follow-up config hygiene:

- Moved `rtx.graphicsPreset = 0` into root `rtx.conf`.
- Added mirrored `scripts/rtx.conf` and `scripts/dxvk.conf` so both root and
  `scripts` working-directory paths see the same active test configuration.

Result from mirrored-config retest:

- The active 64-bit Remix log at `scripts/rtx-remix/logs/remix-dxvk.log` now
  confirms:
  - `Found config file: dxvk.conf`
  - `Found config file: rtx.conf`
  - `rtx.enableRaytracing = True`
  - `rtx.graphicsPreset = 0`
  - `rtx.postfx.enable = False`
  - `rtx.autoExposure.enabled = False`
  - `rtx.bloom.enable = False`
- The old `Automatic Graphics Preset in use` line is gone.
- The log still reports:
  - `Skipped drawcall, using pre-transformed vertices which isn't currently supported.`
  - `Trying to raytrace but not detecting a valid camera.`
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target. Falling back to rasterization`
- ASI summaries show indexed gameplay world draws start after the early camera/UI
  warnings and then reach `indexed=715439`, all `fixedIndexed`, all camera-resend
  eligible.

Next clean retest:

- Re-add the strongest render-target descriptor hash now that `scripts/rtx.conf`
  is confirmed active:

```ini
rtx.raytracedRenderTarget.enable = True
rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB
```

Earlier tests with this hash were likely inconclusive because the active server
config path was not mirrored yet.

Result from clean render-target hash retest:

- User launched, reached gameplay, and quit.
- Active 64-bit server log was `scripts/rtx-remix/logs/remix-dxvk.log`.
- The mirrored config path is confirmed active:
  - `Found config file: dxvk.conf`
  - `Found config file: rtx.conf`
  - `rtx.enableRaytracing = True`
  - `rtx.graphicsPreset = 0`
  - `rtx.postfx.enable = False`
  - `rtx.autoExposure.enabled = False`
  - `rtx.bloom.enable = False`
- The known render-target descriptor hash did not fix the black screen.
- The log still reports:
  - `Skipped drawcall, using pre-transformed vertices which isn't currently supported.`
  - `Trying to raytrace but not detecting a valid camera.`
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target. Falling back to rasterization`

Interpretation: `rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB`
is not sufficient, even when loaded from the active config path. The remaining
lead is that TOD may render the 3D scene into an offscreen render target and
then composite it through a pre-transformed fullscreen/menu-style pass that Remix
does not raytrace as primary geometry.

Follow-up ASI instrumentation:

- Rebuilt `scripts/TODCameraResend.asi` with render-target diagnostics.
- New hooks:
  - `IDirect3DDevice9::CreateTexture`
  - `IDirect3DDevice9::CreateRenderTarget`
  - `IDirect3DDevice9::SetRenderTarget`
  - `IDirect3DDevice9::SetDepthStencilSurface`
  - `IDirect3DDevice9::SetTexture`
  - `IDirect3DDevice9::StretchRect`
- Draw samples now include active render-target and stage-0 texture metadata.

Next test goal: launch once and reach gameplay, then inspect
`rtx-remix/logs/tod-camera-resend.log` for whether fixed-function indexed world
draws are landing on an offscreen render target, and whether a render-target
texture is later sampled by a pre-transformed `DrawPrimitive` fullscreen pass.

Result from render-target diagnostic run:

- Active Remix config path still confirmed `scripts/rtx.conf`.
- `rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB` was parsed and printed
  in the active log, but the black-screen behavior remained.
- Remix still reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target. Falling back to rasterization`

ASI render-target findings:

- TOD creates four render-target textures at startup:
  - `00F2DAA8 -> 1470EFA0`, `2560x1440`, `A8R8G8B8`, render target
  - `1470F098 -> 1470F178`, `2560x1440`, `A8R8G8B8`, render target
  - `1470F678 -> 1470F758`, `1280x720`, `A8R8G8B8`, render target
  - `1470FA58 -> 1470FB38`, `1280x720`, `A8R8G8B8`, render target
- The game sets those texture surfaces as `RT0` thousands of times.
- Real fixed-function indexed gameplay/world draws render into the 2560x1440
  texture-backed RTs, not directly into the backbuffer. Example:
  - `DrawIndexedPrimitive ... rt0=1470F178 rt0Info=2560x1440/A8R8G8B8 rtFromTex=1`
- The game then switches back to the real backbuffer and draws fullscreen
  pre-transformed quads sampling those render-target textures. Example:
  - `DrawPrimitive ... fvf=0x00000144 fvfRHW=1 rt0=00E92BF8 rtFromTex=0 tex0=00F2DAA8 tex0RT=1`

Interpretation:

- The black screen is now strongly explained by TOD's render-to-texture pipeline.
- The main scene is rendered into non-primary RT textures. If Remix does not tag
  the exact descriptor hash for those targets as raytraced render targets, the
  subsequent fullscreen composite has only a black/fallback source.
- The single saved hash `0x1E604227861E0BCB` is not the required hash for the
  active main scene target, or it covers only a non-critical target.

Next config test:

- Updated both `rtx.conf` and `scripts/rtx.conf` to include all render-target
  descriptor hash candidates collected from Remix thumbnails:

```ini
rtx.raytracedRenderTargetTextures = 0xF885C65160789A8B, 0xB566CD7C690A3630, 0x4E44041F9E27548B, 0x4E44041F9E27548E, 0x261F0A146E18E6FA, 0x572A88A0A2E43086, 0x609466F92E702865, 0xD824D823D9CCF030, 0x1E604227861E0BCB
```

If this still fails and the option parses as a hash set, the next step is to get
the exact descriptor hash for the four live render-target textures from Remix UI
or runtime source/hash instrumentation rather than relying on thumbnail guesses.

Result from multi-hash render-target config:

- User launched, entered gameplay, and quit.
- Active server log still confirmed `scripts/rtx.conf`.
- The multi-hash set parsed correctly and was printed by Remix, so this was not
  a config syntax/path failure.
- The black-screen behavior did not change.
- Remix still reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target. Falling back to rasterization`

Interpretation:

- The collected thumbnail hashes are not enough to mark TOD's live 2560x1440
  scene render targets as raytraced targets, or Remix cannot handle this
  render-to-texture/composite path through config alone.
- Since the ASI logs show world geometry is valid fixed-function indexed
  rendering with camera resends, the remaining highest-confidence cause is the
  offscreen scene target followed by a pre-transformed fullscreen composite.

Next ASI bypass test:

- Rebuilt `scripts/TODCameraResend.asi` with two diagnostic switches enabled:
  - Redirect full-size texture-backed scene render targets (`>=2000x1000`,
    `A8R8G8B8`, `fromTexture=1`) to the primary backbuffer.
  - Skip pre-transformed `DrawPrimitive` composites to the primary backbuffer
    when stage 0 is one of TOD's render-target textures.
- New summary counters:
  - `redirectedRT`
  - `skippedRTComposite`

Expected signal:

- If gameplay/menu visuals change, the black screen is confirmed to be caused by
  TOD's offscreen render-target pipeline rather than missing camera state.
- If nothing changes and the new counters remain zero, the redirect heuristic did
  not catch the active scene targets.
- If counters increase but the screen remains black, Remix is still rejecting the
  redirected primary-path geometry, and the next lead is a deeper capture/hash or
  device-state issue rather than the old hash list.

Result from full-size RT bypass test:

- User launched, entered gameplay, and quit.
- The ASI hit the intended path:
  - `redirectedRT` reached `2036`.
  - `skippedRTComposite` reached `2044`.
- The indexed world draw samples moved from texture-backed RTs to the real
  primary backbuffer:
  - `DrawIndexedPrimitive ... rt0Info=2560x1440/X8R8G8B8 rtFromTex=0`
- The active Remix log still reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target. Falling back to rasterization`
- The remaining non-primary RT path in ASI samples is the 1280x720 post-process
  ping-pong:
  - `rt0Info=1280x720/A8R8G8B8 rtFromTex=1`
  - `tex0RT=1 tex0Info=1280x720/A8R8G8B8`

Interpretation:

- The render-to-texture hypothesis is still valid, but the full-size scene RTs
  are not the whole pipeline.
- Since world draws are now on primary but Remix reports `Texture 0 without valid
  hash` exactly when indexed gameplay draws begin, the next lead is texture hash
  validity/capture as well as the 1280x720 post-process RT chain.
- The ASI texture table was capped at 512 while the run created 939 textures, so
  some draw samples showed `tex0Info=0x0/unknown` because the diagnostic table
  overflowed.

Next aggressive ASI bypass test:

- Rebuilt `scripts/TODCameraResend.asi` again with:
  - texture table increased to 4096 entries,
  - all texture-backed `A8R8G8B8` render targets redirected to primary, including
    1280x720 post-process targets,
  - all pre-transformed composites that sample render-target textures skipped,
    even if the destination is another RT texture.

Expected signal:

- If the non-primary RT warning disappears or visuals change, the remaining issue
  is the 1280x720 post-process ping-pong.
- If world draw samples now show valid texture metadata but Remix still says
  `Texture 0 without valid hash`, we should focus on texture hashing/capture
  rather than render targets.

Result from aggressive RT bypass test:

- User launched, entered gameplay, and quit.
- The aggressive redirect caught both the 2560x1440 scene RTs and the 1280x720
  post-process RTs:
  - `redirectedRT` reached `7408`.
  - `skippedRTComposite` reached `21528`.
- The Remix `non-primary, non-raytraced render target` warning disappeared.
- The remaining Remix warnings are:
  - `Skipped drawcall, using pre-transformed vertices which isn't currently supported.`
  - `Trying to raytrace but not detecting a valid camera.`
  - `Texture 0 without valid hash detected, skipping drawcall.`
- The texture table increase worked; world draw samples now have texture
  metadata instead of `0x0/unknown`.
- The first real indexed gameplay draws at the `Texture 0 without valid hash`
  timestamp are on primary and use ordinary game textures:
  - `DrawIndexedPrimitive ... rt0Info=2560x1440/X8R8G8B8 rtFromTex=0`
  - `tex0Info=128x128/OTHER`, `128x256/OTHER`, etc.

Runtime source check:

- Cloned official `NVIDIAGameWorks/dxvk-remix` source to inspect the warning.
- In `src/d3d9/d3d9_rtx.cpp`, the warning is emitted when the first selected
  texture's image hash is `kEmptyHash`:
  - `pTexInfo->GetImage()->getHash() == kEmptyHash`
  - then Remix returns `false` for the RTX draw path.
- In `src/d3d9/d3d9_common_texture.cpp`, the image hash is created in
  `D3D9CommonTexture::SetupForRtxFrom`, from the CPU-side texture buffer.
- In `src/d3d9/d3d9_device.cpp`, managed textures are flushed/hashed earlier
  when `d3d9.evictManagedOnUnlock` is enabled; otherwise upload is deferred.

Next texture-hash timing test:

- Updated both `dxvk.conf` and `scripts/dxvk.conf`:

```ini
d3d9.evictManagedOnUnlock = True
```

Expected signal:

- If the warning disappears or visuals improve, TOD's managed textures were
  being evaluated by Remix before they had image hashes.
- If there is no change, the remaining issue is not only managed-texture upload
  timing; the next step is to inspect whether TOD's compressed texture format or
  texture-stage selection is preventing a valid hash on stage 0.

Result from `d3d9.evictManagedOnUnlock = True`:

- User launched, entered gameplay, and quit.
- The active server log confirmed the option loaded:
  - `d3d9.evictManagedOnUnlock = True`
- The aggressive RT bypass remained effective:
  - the `non-primary, non-raytraced render target` warning stayed absent.
- The black screen remained.
- Remix still reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
- ASI draw samples at the first rejected indexed gameplay draws showed ordinary
  managed DXT1-style textures, not render-target textures:
  - `pool=1` (`D3DPOOL_MANAGED`)
  - `usage=0`
  - format value `827611204` (`DXT1`)
  - examples: `128x128/OTHER`, `128x256/OTHER`

Interpretation:

- The remaining blocker is no longer the offscreen RT chain alone.
- Remix is rejecting primary-backbuffer fixed-function world draws because the
  stage-0 managed texture still has an empty Remix image hash.
- `d3d9.evictManagedOnUnlock = True` was not enough to force these TOD textures
  into Remix's hashed texture path before the draw.

Next ASI texture preload test:

- Rebuilt `scripts/TODCameraResend.asi` with a narrow managed-texture probe:
  - before every `SetTexture`, if the texture is a registered non-render-target
    `D3DPOOL_MANAGED` 2D texture, call `IDirect3DBaseTexture9::PreLoad()`;
  - log `PreLoad texture` entries for the first calls;
  - add `preloadTex` to the ASI summary;
  - add `tex0Usage`, `tex0Pool`, and `tex0Levels` to draw samples.

Expected signal:

- If the black screen or `Texture 0 without valid hash` warning changes,
  Remix needed an explicit managed texture preload before evaluating fixed-
  function world draws.
- If `preloadTex` increases and the warning persists, the next likely workaround
  is a heavier ASI-managed texture copy path: copy managed texture data through a
  `SYSTEMMEM -> DEFAULT` texture pair and bind the default copy so Remix's
  `UpdateTexture`/`SetupForRtxFrom` path computes a hash.

Result from managed texture `PreLoad()` test:

- User launched, reached gameplay, and quit.
- `PreLoad()` definitely ran:
  - ASI summary reached `preloadTex=264007` by present 1020.
- Indexed gameplay/world draws were still fixed-function and on the redirected
  primary backbuffer:
  - `fixedIndexed=371176`
  - `rt0Info=2560x1440/X8R8G8B8`
  - `rtFromTex=0`
- The first indexed gameplay draws at the exact Remix warning timestamp still
  used managed regular textures:
  - `DrawIndexedPrimitive #1` at `04:09:55.986`
  - `tex0Info=128x128/DXT1`
  - `tex0Usage=0`
  - `tex0Pool=1`
- Active Remix log still reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
- The `non-primary, non-raytraced render target` warning remained absent.

Interpretation:

- `IDirect3DBaseTexture9::PreLoad()` is not enough to make Remix hash TOD's
  managed DXT textures.
- The remaining actionable hypothesis is that Remix's hash path only gets
  populated when a `SYSTEMMEM` source is copied into a `DEFAULT` destination via
  `UpdateTexture`, matching the runtime source path in `SetupForRtxFrom`.

Next ASI managed texture copy test:

- Rebuilt `scripts/TODCameraResend.asi` with `PreLoad()` disabled and a stronger
  managed-texture copy/bind workaround enabled.
- For each registered non-render-target `D3DPOOL_MANAGED` 2D texture:
  - create a temporary `D3DPOOL_SYSTEMMEM` texture;
  - create a persistent `D3DPOOL_DEFAULT` texture;
  - lock/copy all supported mip levels from managed source to system memory;
  - call `UpdateTexture(systemmem, default)`;
  - bind the default-pool copy instead of the original managed texture.
- New ASI summary counters:
  - `copyAttempts`
  - `copySuccess`
  - `copyFailures`
  - `copyBinds`

Expected signal:

- If Remix's empty-hash warning disappears or moves later, the black screen is
  blocked by TOD's managed texture upload/hash path.
- If `copySuccess` and `copyBinds` increase but the same warning remains on the
  first indexed draw, the failure is deeper than ordinary managed texture
  hashing and may require modifying Remix itself or disabling textured material
  validation for this game.

Result from broad managed texture copy test:

- User reported a major visible improvement:
  - cutscenes lagged/glitched;
  - gameplay became much more visible.
- Active Remix log no longer reported:
  - `Texture 0 without valid hash detected, skipping drawcall.`
  - `Found a draw call to a non-primary, non-raytraced render target.`
- Remix proceeded into real RTX rendering:
  - `[RTX] Opacity Micromap: enabled`
  - `RenderPass GBuffer Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Direct Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Indirect Raytrace Mode: Trace Ray (RGS)`
- ASI counters near the end of the run:
  - `copyAttempts=919`
  - `copySuccess=919`
  - `copyFailures=0`
  - `copyBinds=4689745`
- First gameplay draw now used a default-pool copied DXT1 texture:
  - `DrawIndexedPrimitive #1`
  - `tex0Info=128x128/DXT1`
  - `tex0Pool=0`

Interpretation:

- The black screen's main blocker is confirmed: TOD's normal managed textures
  are not getting valid Remix image hashes when bound directly.
- Copying managed textures through `SYSTEMMEM -> DEFAULT` makes Remix compute
  hashes and allows the RTX path to proceed.
- The broad workaround is too invasive because it also copies dynamic/video/UI
  A8R8G8B8 textures, including 640x360 cutscene textures, which likely explains
  the lag and glitches.

Next narrowed ASI test:

- Rebuilt `scripts/TODCameraResend.asi` to copy only compressed managed textures
  (`DXT1`, `DXT3`, `DXT5`, etc.).
- Non-compressed managed textures such as A8R8G8B8 cutscene/UI textures are left
  on the original managed texture path.

Expected signal:

- If gameplay remains visible and cutscene/menu glitches improve, the stable
  workaround should stay DXT-only.
- If gameplay becomes black or many objects disappear, add selective copying for
  specific non-compressed world textures while keeping 640x360 video/cutscene
  textures excluded.

Result from DXT-only copy test:

- User reported:
  - video cutscenes and intro now work better;
  - gameplay remains visible but still has glitches;
  - main-menu/text visibility is still a priority.
- Active Remix log still reported one empty-hash warning:
  - `Texture 0 without valid hash detected, skipping drawcall.`
- The warning timestamp aligned with the first A8R8G8B8 managed texture after
  the initial DXT1 world draws:
  - `DrawIndexedPrimitive #14`
  - `tex0Info=32x32/A8R8G8B8`
  - `tex0Pool=1`
- The DXT1 world textures were successfully default-pool copies:
  - first DXT1 draws show `tex0Pool=0`
- The ASI texture metadata table filled during the long run:
  - `textures=4096`
  - `createTex=3945`

Interpretation:

- DXT-only is much less invasive and preserves video, but it is not enough.
- Some uncompressed A8R8G8B8 textures are still used by real indexed gameplay
  draws and by text/UI-style paths.
- The 4096-entry texture table is too small once default copies are registered.

Next selective A8R8G8B8 test:

- Rebuilt `scripts/TODCameraResend.asi` with:
  - texture metadata table increased to 16384 entries;
  - copy-info table increased to 8192 entries;
  - DXT copy still enabled;
  - A8R8G8B8 copy enabled only when the texture is not the known 640x360
    video/cutscene texture size.
- Mirrored the saved Remix UI texture opinion into root `rtx.conf`:

```ini
rtx.uiTextures = 0xF8A7EB3A1A6FE3FB, -0x7ECA4C36B688596C
```

Expected signal:

- If gameplay glitches reduce and video remains good, keep this selective copy
  policy.
- If video regresses, tighten the exclusion beyond `640x360`.
- If menu text still does not appear, use Remix Game Setup to tag the likely
  font/menu textures again now that A8R8G8B8 UI textures can get hashes.

Result from broad non-video A8R8G8B8 copy test:

- User reported:
  - mission 1 improved drastically;
  - HUD appeared in mission 1;
  - missions 2 and 3 became much darker / more broken than the previous DXT-only
    run.
- Active Remix log no longer showed the texture-hash warning after RTX startup,
  so the broad A8 copy did solve the remaining empty-hash path.
- ASI summary showed heavy A8/default copy usage:
  - `copyAttempts=1277`
  - `copySuccess=1277`
  - `copyFailures=0`
  - `copyBinds=2911912`
- Early copy logs showed many small/mid-sized A8R8G8B8 textures copied before
  and during gameplay, including likely mission/light/effect textures.

Interpretation:

- A8R8G8B8 copying helps HUD/text and fixes the final texture-hash warning, but
  copying all non-video A8R8G8B8 textures is too broad.
- Some A8R8G8B8 textures are probably mission-specific dynamic light/effect
  textures. Copying them once into default-pool replacements can freeze or
  misrepresent lighting in later missions.

Next layout-aware A8R8G8B8 test:

- Rebuilt `scripts/TODCameraResend.asi` with:
  - DXT managed texture copies still enabled globally;
  - A8R8G8B8 managed texture copies enabled only when the current D3D layout is
    pre-transformed (`XYZRHW` / UI-HUD-menu style);
  - normal indexed world geometry no longer triggers A8R8G8B8 copies.

Expected signal:

- Missions 2 and 3 should move back toward the DXT-only lighting behavior.
- HUD/menu/text may remain improved because those draws are pre-transformed.
- If text disappears again, the next step is to explicitly copy only small
  A8R8G8B8 font/HUD dimensions instead of all pre-transformed A8 textures.

Result from layout-aware A8R8G8B8 copy test:

- User reported:
  - mission 1 HUD disappeared again;
  - many structures became black again;
  - missions 2 and 3 returned closer to the pre-broad-A8 behavior;
  - cars looked more incorrect / metal-scrap-like.
- Active Remix log again showed:
  - `Texture 0 without valid hash detected, skipping drawcall.`
- ASI summary showed the currently bound missing-style texture was still a small
  managed A8R8G8B8 texture:
  - `tex0Info=32x16/A8R8G8B8`
  - `tex0Pool=1`
- The layout-aware condition missed some HUD/text-relevant textures because
  `SetTexture` often happens before the later pre-transformed draw layout is set.

Interpretation:

- Layout state at `SetTexture` time is not reliable enough to decide whether an
  A8R8G8B8 texture is UI/HUD.
- Broad A8 copying is too much; layout-only A8 copying is too little.

Next A8 whitelist test:

- Rebuilt `scripts/TODCameraResend.asi` with:
  - DXT managed texture copies still enabled globally;
  - A8R8G8B8 copies no longer based on current layout;
  - A8R8G8B8 copies restricted to likely UI/font/HUD dimensions and one-mip
    atlases:
    - `1024x128`
    - `512x512`
    - `256x256`
    - `128x128`
    - `64x64`
    - `8x8`
    - very small one-mip textures up to `32x32`
  - known `640x360` video/cutscene textures still excluded.

Expected signal:

- HUD/menu text should improve compared with layout-aware A8.
- Missions 2 and 3 should stay closer to DXT-only because mipmapped A8 world /
  light/effect textures are no longer copied broadly.
- If cars remain metal-like, that is probably a separate material-classification
  issue rather than the main texture-hash black-screen bug.
