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

Result / decision on the A8 whitelist approach:

- The user correctly flagged this as an unclean direction. Choosing UI/HUD
  textures from dimensions such as `1024x128`, `512x512`, or `32x16` is only a
  diagnostic shortcut, not a stable compatibility fix.
- The underlying problem is not "which A8 texture size should be copied"; it is
  that TOD uses normal `D3DPOOL_MANAGED` textures that Remix does not hash when
  they are bound directly. The broad copy path fixed this by giving Remix
  default-pool textures, but it froze or misrepresented dynamic A8 textures.

## 2026-06-01 - Clean Managed Texture Shadow Test

Rebuilt `scripts/TODCameraResend.asi` with the whitelist removed.

New active approach:

- Every supported non-render-target `D3DPOOL_MANAGED` texture gets a persistent
  shadow pair:
  - source: original game managed texture;
  - staging: ASI-created `D3DPOOL_SYSTEMMEM` texture;
  - replacement: ASI-created `D3DPOOL_DEFAULT` texture bound to the device.
- Supported formats now include DXT textures and common uncompressed UI/light
  formats (`A8R8G8B8`, `X8R8G8B8`, `R5G6B5`, `A8`, `A8L8`, `L8`, etc.).
- The ASI hooks `IDirect3DTexture9::LockRect`,
  `IDirect3DTexture9::UnlockRect`, and `IDirect3DTexture9::AddDirtyRect`.
- Write locks and dirty rects mark the shadow dirty.
- On `SetTexture`, the ASI refreshes the shadow with
  `CopyTextureData(source, staging)` followed by
  `UpdateTexture(staging, default)` only if the source was dirtied or has never
  been uploaded.
- If a refresh fails, the ASI binds the original texture instead of a stale
  default-pool copy.

Why this is cleaner:

- No dimension whitelist.
- No special-case UI/font guesswork in the ASI.
- Dynamic video, menu, HUD, light, and effect textures are allowed to update
  instead of being frozen after the first copy.
- RTX Remix UI texture tagging remains in `rtx.uiTextures`, where it belongs,
  but the ASI's job is only to make hashes possible.

Active config cleanup:

- Removed `d3d9.evictManagedOnUnlock = True` from both `dxvk.conf` files. It was
  confirmed loaded earlier but did not fix the empty-hash path.
- Removed the guessed `rtx.raytracedRenderTargetTextures` list from both
  `rtx.conf` files. The ASI redirects TOD's offscreen color render targets to
  the primary path and skips the old fullscreen RT composites, so the guessed
  thumbnail hashes should not remain active as a hidden variable.
- Kept:

```ini
rtx.enableRaytracing = True
rtx.graphicsPreset = 0
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.bloom.enable = False
rtx.uiTextures = 0xF8A7EB3A1A6FE3FB, -0x7ECA4C36B688596C
```

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next test signal:

- Launch the game, enter the main menu and at least one gameplay mission, then
  quit.
- Check whether text/HUD/menu improves without the mission 2/3 darkness
  regression from the broad one-time A8 copy path.
- In `rtx-remix/logs/tod-camera-resend.log`, useful counters are now:
  - `dirtyMarks`
  - `shadowUploads`
  - `shadowUploadFailures`
  - `texLocks`
  - `texUnlocks`
  - `dirtyRects`
- In `remix-dxvk.log`, the key regression check is whether
  `Texture 0 without valid hash detected, skipping drawcall` returns.

## 2026-06-01 - Dirty Shadow Regression: Texture Swaps

User result from the first dirty-shadow build:

- First mission was visible, but assets were severely mixed up: textures appeared
  on the wrong objects.

Interpretation:

- This is likely ASI shadow bookkeeping, not an RTX material setting.
- The first dirty-shadow build keyed shadow replacements only by the raw
  `IDirect3DTexture9*` source pointer.
- D3D/game allocators can destroy and recreate textures, potentially reusing the
  same COM pointer value later. If the ASI keeps the old default-pool shadow for
  that pointer, a new source texture can bind an unrelated old replacement.
- Descriptor mismatch can also catch some cases, but it does not catch pointer
  reuse where the new texture has the same dimensions/format/level count.

Fix built:

- Added `IDirect3DTexture9::Release` hook.
- When a source texture's release count reaches zero, the ASI now:
  - releases its staging/default shadow textures;
  - clears the texture-copy table entry;
  - clears the texture metadata entry.
- Each shadow now stores its source descriptor:
  - width;
  - height;
  - mip levels;
  - format.
- Before binding a shadow, the ASI validates the current source descriptor
  against the stored shadow descriptor.
- If the descriptor changed, the stale shadow is released and rebuilt.
- Texture/copy metadata tables now reuse cleared slots instead of only appending.

New counters in `tod-camera-resend.log`:

- `staleShadows`
- `texReleases`

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next test signal:

- If the texture-swap symptom disappears, pointer reuse was the main bug in the
  clean shadow approach.
- If assets are still swapped but `staleShadows` / `texReleases` are active, the
  next fix is to hook a stronger lifecycle or generation signal.
- If assets are no longer swapped but dynamic textures remain wrong, the next
  issue is missed dirty marking rather than stale shadow reuse.

## 2026-06-01 - Sky / Remaining Texture Mess Follow-up

User result from recording:

- First mission no longer looks like total black-screen failure.
- Textures still look wrong in places, with the sky called out as a clear
  problem.
- Recording referenced:
  `C:\Users\utkar\Videos\NVIDIA\Total Overdose\Total Overdose 2026.06.01 - 19.48.48.02.mp4`

Log signal from that run:

- `staleShadows=0`
- `shadowUploadFailures=0`
- `texReleases` is active, so the release hook is working.
- This makes stale source-pointer reuse unlikely as the remaining primary cause.

Interpretation:

- The ASI was still replacing managed textures at `SetTexture` time. That is too
  early to know whether a texture is being used as world albedo, UI, video, a
  lightmap/mask, or sky.
- Copying all uncompressed `A8R8G8B8` world textures gives Remix hashes, but it
  can also make Remix treat masks/lightmaps/effects as ordinary surface albedo.
  That matches the remaining "wrong textures / weird sky" look.

Fix built:

- Moved managed texture substitution from `SetTexture` time to draw time.
- `SetTexture` now records the game's source texture and binds the original.
- Immediately before a draw, the ASI applies a conservative policy for stage 0:
  - DXT/compressed managed textures are shadowed for world and UI draws.
  - uncompressed managed textures are shadowed only when the draw layout is
    pre-transformed (`XYZRHW` / screenspace UI, menu, video, fullscreen-style
    draw).
  - uncompressed textures on normal indexed world geometry are left as the
    original managed texture for now.
- This removes the old dimension whitelist while avoiding broad A8 world
  substitution.
- New log counter:
  - `drawTimeBinds`

Sky config test:

- Added to both active `rtx.conf` paths:

```ini
rtx.skyAutoDetect = 2
```

Reason:

- The RTX Remix options document `rtx.skyAutoDetect = 2` as
  CameraPositionAndDepthFlags sky detection. This is a conservative first test
  before manually tagging sky texture/geometry hashes.
- If this helps, the clean long-term fix is to tag the actual sky texture or
  geometry in Remix Game Setup and save it as `rtx.skyBoxTextures` or
  `rtx.skyBoxGeometries`.

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next test signal:

- Launch first mission again and compare:
  - whether general texture mixing is reduced;
  - whether sky improves with `rtx.skyAutoDetect = 2`;
  - whether HUD/menu text remains visible.
- After quitting, inspect:
  - `drawTimeBinds`
  - `copyBinds`
  - `Texture 0 without valid hash detected`
  - any sky-related visible change.

## 2026-06-01 - Draw-Time Texture Policy Result

User result:

- Significant improvement in first mission.
- Recording:
  `C:\Users\utkar\Videos\NVIDIA\Total Overdose\Total Overdose 2026.06.01 - 20.09.48.03.mp4`
- Extracted review frames:
  `re_docs\video_frames_200948\frame_001.jpg` through
  `re_docs\video_frames_200948\frame_008.jpg`

Visual findings from extracted frames:

- General world texture assignment is much more coherent than the broad A8 or
  first dirty-shadow builds.
- Main remaining obvious issue is sky/background classification:
  - top/background sky is still black;
  - far terrain/large background sheets can look like ordinary lit scene
    geometry instead of sky/backdrop.
- Interior lighting and geometry are now visible enough to debug normal Remix
  categories.

Runtime log findings:

- Active `scripts\rtx.conf` was loaded and parsed:
  - `rtx.enableRaytracing = True`
  - `rtx.graphicsPreset = 0`
  - `rtx.skyAutoDetect = 2`
  - expanded `rtx.uiTextures` list saved by Remix UI
- RTX path is active:
  - `RenderPass GBuffer Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Direct Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Indirect Raytrace Mode: Trace Ray (RGS)`
- ASI summary during gameplay shows:
  - `copyFailures=0`
  - `shadowUploadFailures=0`
  - `staleShadows=0`
  - `drawTimeBinds` active
- One `Texture 0 without valid hash detected` remains at gameplay entry.
  Local ASI samples around the same period show the likely skipped draw is a
  normal world draw using a managed `32x32/A8R8G8B8` texture. That is expected
  with the conservative policy because broad A8 world shadowing previously
  caused the severe texture/material corruption.

Interpretation:

- The current ASI draw-time policy is the best baseline so far.
- Do not return to broad A8 world texture shadowing.
- The remaining black sky is probably no longer a generic texture hash problem.
  `rtx.skyAutoDetect = 2` parsed but did not fully classify Total Overdose's
  sky/backdrop path.

Next clean sky fix:

- Use RTX Remix Game Setup / Developer UI to tag the actual sky:
  - preferred: tag sky geometry/object if the black/backdrop sheet is selectable
    in Object/Geometry view;
  - fallback: tag the sky/backdrop texture if the texture thumbnail/hash is
    clearly visible.
- Save as one of:

```ini
rtx.skyBoxGeometries = ...
rtx.skyBoxTextures = ...
```

Config hygiene:

- Mirrored the expanded `rtx.uiTextures` list from `scripts\rtx.conf` into root
  `rtx.conf` so both config search paths stay consistent.

## 2026-06-01 - Clean Programmatic UI Candidate Path

User concern:

- Main menu/HUD text still needs a clean fix.
- Sky is not selectable in Remix UI.
- Do not hardcode Total Overdose content hashes or dimensions into the ASI.

Decision:

- Keep the ASI as a D3D state/capture layer only.
- Do not make the ASI decide "this exact texture hash is UI/sky/light".
- Programmatically identify candidate roles from rendering behavior:
  - UI/menu/HUD candidates: pre-transformed screen-space draws
    (`XYZRHW`/`POSITIONT`) using non-render-target managed textures.
  - Video candidates: large one-mip screen-space `A8R8G8B8` textures, kept
    separate from UI so FMV frames do not pollute UI tagging.
  - World unshadowed candidates: regular world draws using supported managed
    textures that are intentionally not substituted by the conservative
    draw-time policy.

Implemented:

- Added a separate evidence log:

```text
rtx-remix\logs\tod-remix-candidates.tsv
```

- Each row records:
  - role candidate;
  - draw type/count;
  - texture pointer;
  - D3D descriptor: size, format, levels, usage, pool;
  - `rtxTextureHash`;
  - draw layout flags;
  - current render target descriptor.

RTX hash mapping:

- Pulled official `NVIDIAGameWorks/dxvk-remix` source under
  `third_party\dxvk-remix`.
- Source check:
  - `D3D9Rtx::checkBoundTextureCategory()` compares category config entries to
    `texture->GetSampleView(false)->image()->getHash()`.
  - `D3D9CommonTexture::SetupForRtxFrom()` normally computes that image hash as
    `XXH3_64bits()` over mip 0's CPU upload buffer.
  - `rtx.useObsoleteHashOnTextureUpload = False` by default; if enabled, Remix
    uses the older `XXH64()` path instead.
- Updated the ASI candidate logger to compute the normal RTX texture hash path
  for common 2D formats instead of a private ASI fingerprint.
- This should allow programmatic `rtx.uiTextures` suggestions from UI candidate
  rows after one run, without hardcoding Total Overdose content in the ASI.

Added analyzer:

```powershell
powershell -ExecutionPolicy Bypass -File tools\analyze_remix_candidates.ps1
```

Output:

```text
re_docs\rtx_remix_candidate_report.md
```

Current build status:

- `TOD.exe` and `NvRemixBridge.exe` were closed.
- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to:

```text
scripts\TODCameraResend.asi
```

Next run:

- Launch the game, enter the black/menu/gameplay cases, then quit.
- Inspect `rtx-remix\logs\tod-remix-candidates.tsv`.
- Generate the markdown report:

```powershell
powershell -ExecutionPolicy Bypass -File tools\analyze_remix_candidates.ps1
```

- The report will list UI candidate `rtxTextureHash` values and a merged
  `rtx.uiTextures` config snippet.

## 2026-06-01 - UI Candidate Report Applied

Run result:

- `rtx-remix\logs\tod-remix-candidates.tsv` was generated.
- Candidate counts:
  - `ui_candidate`: 1814 rows
  - `video_candidate`: 8 rows
  - `world_unshadowed_candidate`: 226 rows
- `tools\analyze_remix_candidates.ps1` wrote:

```text
re_docs\rtx_remix_candidate_report.md
```

Important correction:

- Remix hash-set config uses `-0x...` as a negative/removal entry, not a
  signed representation of a high-bit hash.
- Therefore `-0xA46C307BB6FFCB19` was actively removing that texture from UI
  classification.
- `0xA46C307BB6FFCB19` was detected as a high-confidence screen-space
  `1024x512/DXT1` UI atlas, so the negative entry was removed and the positive
  entry was added.

Applied targeted `rtx.uiTextures` update in both active config paths:

```ini
rtx.uiTextures = 0x1D1D8E3DB8CE7310, 0xDDF1BE54CD1936C8, 0xF8A7EB3A1A6FE3FB, 0xA46C307BB6FFCB19, 0xD84E0241FB4E93F6, 0x6D903948AD06648A, 0x2B4816BD87376AA3, -0x51D36BDF7C5C3E9D, -0x6F26C2C14C981112, -0x72EAE28ED2166958, -0x7ECA4C36B688596C
```

New high-confidence positive UI hashes:

- `0xA46C307BB6FFCB19` - screen-space `1024x512/DXT1`, likely menu/UI atlas.
- `0xD84E0241FB4E93F6` - screen-space `1024x128/A8R8G8B8`, likely font/HUD atlas.
- `0x6D903948AD06648A` - screen-space `512x32/A8R8G8B8`, likely narrow text/strip atlas.
- `0x2B4816BD87376AA3` - screen-space `1024x128/A8R8G8B8`, likely another font/HUD atlas.

Next test:

- Launch the game and check main menu text first.
- If the screen becomes worse or unexpected 2D overlays appear as UI, revert
  only the four new positive hashes from the `rtx.uiTextures` line.

## 2026-06-01 - UI Config Test Result

User result:

- Main menu is fine with the targeted UI config update.

Runtime confirmation:

- Latest `scripts\rtx-remix\logs\remix-dxvk.log` loaded:

```ini
rtx.skyAutoDetect = 2
rtx.uiTextures = 0x1D1D8E3DB8CE7310, 0x2B4816BD87376AA3, 0x6D903948AD06648A, 0xA46C307BB6FFCB19, 0xD84E0241FB4E93F6, 0xDDF1BE54CD1936C8, 0xF8A7EB3A1A6FE3FB, -0x51D36BDF7C5C3E9D, -0x6F26C2C14C981112, -0x72EAE28ED2166958, -0x7ECA4C36B688596C
```

Regenerated report:

- `re_docs\rtx_remix_candidate_report.md`
- Current UI config has:
  - 7 positive UI hashes;
  - 4 negative/removal entries;
  - 0 new high-confidence UI hashes after the latest run.

Decision:

- Keep the current UI config.
- Do not broaden `rtx.uiTextures` with the raw screen-space candidate set.
- Next clean target is sky/background classification, then baked lighting or
  lightmap classification for the dark/green interior room.

## 2026-06-01 - Pause Menu / HUD Debug Setup

User result:

- Main menu is fixed.
- Pause menu and HUD are still not working correctly.

Interpretation:

- Main menu and HUD/pause are likely not sharing the exact same texture set.
- The current `rtx.uiTextures` line intentionally includes only high-confidence
  large atlas/font hashes. The latest candidate log contains many smaller
  screen-space DXT/A8 textures that could be HUD icons, pause widgets, or
  per-state glyph strips.
- Adding all raw screen-space candidates would be too broad and could classify
  videos/post effects/world overlays as UI.

Implemented clean next step:

- ASI now dumps new screen-space UI candidate textures as DDS files:

```text
rtx-remix\logs\tod-candidate-textures
```

- Filename format:

```text
ui_candidate_<rtxTextureHash>_<width>x<height>_<format>.dds
```

- ASI also now records actual `GetLevelCount()` instead of the raw
  `CreateTexture(Levels=0)` argument, which makes candidate filtering less
  misleading.

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch game.
- Enter gameplay until HUD should be visible.
- Open the pause menu.
- Quit.
- Then inspect/convert the DDS dumps and add only the confirmed pause/HUD atlas
  hashes to `rtx.uiTextures`.

## 2026-06-01 - Pause Menu / HUD Render-Target Composite Pass

User result after the DDS dump build:

- Main menu still works.
- Pause menu and HUD are still not visible correctly.

Candidate texture review:

- Latest run produced `851` DDS files under
  `rtx-remix\logs\tod-candidate-textures`.
- Converted/labeled sheets in `re_docs\hud_pause_candidate_png_2100` show that
  most screen-space candidates are world textures drawn through sprite-like
  paths, not safe UI config entries.
- The obvious menu/font atlases are already in `rtx.uiTextures`:
  `0xD84E0241FB4E93F6`, `0x6D903948AD06648A`,
  `0x2B4816BD87376AA3`, `0xA46C307BB6FFCB19`.
- The only new unique high-res candidate observed in that run was
  `0x964B4ED809103869`, a cutscene/title-card texture, not HUD/pause UI.

Runtime signal:

- `tod-camera-resend.log` shows thousands of skipped full-screen
  render-target composites.
- During gameplay, the game repeatedly composites source render-target textures
  sized `2560x1440/A8R8G8B8` and `1280x720/A8R8G8B8` back to the primary
  `2560x1440/X8R8G8B8` target.
- The `1280x720/A8R8G8B8` render targets are also currently redirected to the
  primary backbuffer:

```text
SetRenderTarget ... requested=... reqInfo=1280x720/A8R8G8B8 reqFromTex=1
actual=... actualInfo=2560x1440/X8R8G8B8 actualFromTex=0 redirected=1
```

Interpretation:

- Pause/HUD is now more likely blocked by the ASI render-target workaround than
  by missing `rtx.uiTextures` hashes.
- The broad skip/redirect path is still needed for the original black-screen
  scene render targets, but it can also hide UI if the game renders a HUD or
  pause layer into an offscreen target and composites it later.

Implemented clean test:

- Added behavior-based render-target classification to
  `tools\tod_camera_resend_asi\TODCameraResend.cpp`.
- The ASI now tracks the render target the game requested, separately from the
  actual redirected target.
- Smaller color render targets can be marked as UI render targets only when the
  game draws pre-transformed managed UI-like textures into them.
- Full-size scene render targets are not promoted by this pass
  (`kPromoteFullSizeBehaviorMarkedUiRenderTargets = false`) to avoid reopening
  the original black-screen path.
- A marked target is not allowed to composite until it has had at least one
  native, non-redirected `SetRenderTarget` pass. This avoids compositing stale
  redirected contents on the same frame it was first detected.
- Throttled redirected `SetRenderTarget` logging to every 500th call so the new
  pause/HUD markers are readable.

New log markers:

```text
marked behavior UI RT draw
native behavior UI RT SetRenderTarget
allowed behavior UI RT composite DrawPrimitive
```

New summary counters:

```text
allowedUiRTComposite
uiRtMarks
uiRtNativeSets
uiRtInfos
```

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch the game.
- Enter gameplay where HUD should be visible.
- Open the pause menu.
- Stay there for a few seconds so the behavior-marked RT can be used on a
  following frame.
- Quit, then inspect the new log markers/counters before changing any more
  `rtx.uiTextures` hashes.

## 2026-06-01 - Pause/HUD Follow-up: Identity View Guard

User ran the behavior-marked render-target build.

Fresh run result:

- `uiRtMarks = 0`
- `allowedUiRTComposite = 0`
- `uiRtNativeSets = 0`
- `skippedRTComposite` continued increasing, mostly for `1280x720` and
  `2560x1440` render-target composites.

Interpretation:

- The missing pause/HUD path was not observed as "draw managed UI textures into
  a smaller offscreen render target, then composite it later".
- Fresh UI candidate rows are direct-to-primary (`rtFromTexture = 0`).
- The only new dumped high-res UI candidate was:
  `0x8D86587F12AACD19`, a cutscene/title-card texture. Do not add it to
  `rtx.uiTextures`.

More important runtime signal:

- Gameplay-period HUD-looking candidates appear as
  `world_unshadowed_candidate`, not `ui_candidate`.
- They are often `DrawIndexedPrimitive` fixed-function draws with FVF values
  such as `0x00000112`, not RHW/pre-transformed draws.
- The ASI was resending the cached 3D camera before every non-pretransformed
  fixed-function indexed draw.
- The game also sets identity `D3DTS_VIEW` roughly once per frame. That is a
  strong signal for a 2D/overlay pass. Resending the 3D camera after that can
  move or light HUD/pause geometry as if it were world geometry.

Implemented clean fix:

- Added `g_currentViewIsIdentity` tracking.
- `Hook_SetTransform(D3DTS_VIEW)` now records whether the current view is
  identity.
- `ResendCameraForDraw()` now skips camera resend while the current view is
  identity.
- This preserves the game's intentional 2D/overlay transform state without
  hardcoding any texture hash, mission, or HUD asset.

New log marker/counter:

```text
skipped camera resend for identity-view draw
identityViewResendSkips
```

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch the game and enter gameplay.
- Check whether HUD and pause menu become visible or less distorted.
- Quit and inspect:
  - `identityViewResendSkips`
  - `ignoredIdentityViews`
  - whether `resends` drops around overlay passes
  - whether HUD/pause candidates still appear only as `world_unshadowed_candidate`

## 2026-06-01 - Pause/HUD Follow-up: Render-State Overlay Classifier

Fresh run after the identity-view guard:

- `identityViewResendSkips = 0`
- `ignoredIdentityViews` continued increasing, but no indexed draws happened
  while the current tracked view was identity.
- Therefore the identity-view guard did not affect the observed pause/HUD path.

Current interpretation:

- Pause/HUD is still likely in the direct-to-primary draw path, not an offscreen
  UI render-target composite.
- Many HUD-looking candidates remain `world_unshadowed_candidate` because they
  are not RHW/pre-transformed; they are fixed-function indexed draws with FVF
  values such as `0x00000112`.
- The clean next discriminator is render state. HUD/pause overlays should use
  depth-disabled and alpha-enabled state even when the vertices are not RHW.

Implemented:

- Added `IDirect3DDevice9::SetRenderState` hook.
- Tracked:
  - `D3DRS_ZENABLE`
  - `D3DRS_ZWRITEENABLE`
  - `D3DRS_ALPHABLENDENABLE`
  - `D3DRS_ALPHATESTENABLE`
  - `D3DRS_LIGHTING`
  - `D3DRS_FOGENABLE`
- Added behavior overlay state:

```text
(Z disabled OR Z write disabled) AND (alpha blend enabled OR alpha test enabled)
```

- `ShouldShadowTextureForDraw()` now treats uncompressed managed textures as
  shadowable when this overlay state is active on the primary target, even if
  the draw is not RHW.
- Candidate rows now include render-state columns:
  `zEnable`, `zWriteEnable`, `alphaBlendEnable`, `alphaTestEnable`, `lighting`,
  `fogEnable`, `overlayState`.
- Non-RHW overlay-state draws are now reported as `ui_candidate` instead of
  `world_unshadowed_candidate`.

New log counters:

```text
setRS
overlayStateBinds
```

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch the game, enter gameplay, open pause menu, then quit.
- Inspect:
  - `overlayStateBinds`
  - `SetRenderState ... overlayState=1`
  - whether former HUD-looking `world_unshadowed_candidate` rows become
    `ui_candidate` rows with `overlayState=1`
  - whether HUD/pause text becomes visible without broad texture corruption.

## 2026-06-01 - Clean UI Config Hash Shadow Test

Reason for changing approach:

- The render-state overlay classifier fired heavily, but did not recover the
  remaining HUD/pause path.
- Broad uncompressed managed texture shadowing previously made some HUD show up,
  but it also corrupted world textures, mission lighting, vehicles, and sky-like
  assets.
- The latest high-resolution candidate review did not reveal safe generic UI
  dimensions. Reviewed candidates were title/cutscene cards, character sheets, or
  foliage/vegetation, so adding a dimension whitelist would be too fragile.
- Clean rule: let RTX Remix config own the UI decision. If a texture hash is in
  `rtx.uiTextures`, the ASI may bind the default-pool shadow copy for that
  texture even when the draw is not RHW/pre-transformed.

Implemented:

- ASI now parses `rtx.uiTextures` from both:
  - `rtx.conf`
  - `scripts\rtx.conf`
- Positive hashes are added; negative entries such as `-0x...` remove hashes,
  matching dxvk-remix config-set semantics.
- Managed texture RTX hashes are cached per texture and invalidated when the
  texture is marked dirty through `UnlockRect` or `AddDirtyRect`.
- `ShouldShadowTextureForDraw()` now shadows uncompressed managed textures when
  their cached RTX hash is already configured as UI.
- No Total Overdose-specific UI hash whitelist was hardcoded into the ASI.

New log lines/counters:

```text
loaded configured RTX UI texture hashes: count=N
configured RTX UI texture hash hit #...
configuredUiBinds
configuredUiHashHits
configuredUiHashes
```

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully.
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch the game, enter gameplay, open pause menu, then quit.
- Inspect whether HUD/pause/menu text improves.
- Check the summary for `configuredUiBinds > 0` and `configuredUiHashHits > 0`.
- If those stay zero, the remaining HUD/pause textures are not currently in
  `rtx.uiTextures`; the next clean step is to identify the real HUD/pause hashes
  from candidate dumps and add only confirmed UI hashes to config.

## 2026-06-01 - In-World HUD Candidate Discovery

User result from the configured-UI-hash shadow build:

- Still no visible in-game HUD.

Root-cause refinement:

- The HUD/pause path in Total Overdose is not RHW/screen-space UI. It is drawn as
  fixed-function indexed geometry (FVF such as `0x112`) under a non-identity view,
  i.e. through the same 3D pipeline as the world.
- Consequence chain that hides it:
  - the identity-view guard never fires on these draws
    (`identityViewResendSkips = 0`);
  - the candidate logger lumped them into `world_unshadowed_candidate`, so they
    were never DDS-dumped and their hashes were never surfaced;
  - the conservative draw-time policy leaves their `A8R8G8B8` textures unshadowed,
    so Remix reports `Texture 0 without valid hash` and skips the drawcall.
- The clean Remix mechanism for these is `rtx.uiTextures` (rasterized overlay, not
  raytraced). That needs the exact texture hashes, which first requires the draws
  to be discoverable. This was a chicken-and-egg discovery gap, not a new render
  bug.

Implemented (additive, read-only, no rendering-behavior change):

- Added a distinct `hud_candidate` role
  (`kCandidateRoleHud`) for fixed-function indexed draws that run with overlay
  render state on the primary target. `IsFixedFunctionOverlayDraw()` isolates this
  case; `IsBehaviorOverlayDraw()` now composes from it so existing shadow behavior
  is unchanged.
- `hud_candidate` draws are DDS-dumped to a dedicated directory so they stay out
  of the noisy screen-space `ui_candidate` set:

```text
rtx-remix\logs\tod-candidate-hud
```

- Candidate TSV rows gained two content-agnostic discovery columns, written for
  every role:
  - `frameDrawIndex` - draw position within the frame (reset at `Present`).
    HUD/pause overlays cluster at the end of the frame.
  - `projOrtho` - whether the active projection is orthographic
    (`_34 ~= 0`, `_44 ~= 1`), a clean 2D-overlay signal.
- These columns are appended at the end of the row, so the existing
  column-name-based analyzer keeps working.
- New summary counter: `hudCandidateRows`.

Analyzer updates (`tools\analyze_remix_candidates.ps1`):

- `hud_candidate` added to the per-role summary tables.
- New `## In-World HUD Candidates For Visual Review` section lists HUD-class
  hashes with `frameDrawIndex` and `projOrtho`, plus the expected
  `hud_candidate_*.dds` filenames.

Why this is the clean path:

- No Total Overdose content (hash, dimension, mission) is hardcoded in the ASI.
- The HUD signature is derived purely from draw layout, render state, projection,
  and frame ordering.
- Even if the overlay-state heuristic misses a HUD draw, `frameDrawIndex` and
  `projOrtho` now appear on `world_unshadowed_candidate` rows too, so late-frame
  ortho draws can still be found manually.

Build result:

- `cmd /c tools\tod_camera_resend_asi\build.bat` completed successfully (`/W4`,
  no warnings).
- Output installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch the game, enter gameplay until the HUD is visible, open the pause menu,
  then quit.
- Generate the report:

```powershell
powershell -ExecutionPolicy Bypass -File tools\analyze_remix_candidates.ps1
```

- Inspect the `## In-World HUD Candidates For Visual Review` table and the DDS
  dumps in `rtx-remix\logs\tod-candidate-hud`.
- Add only the confirmed HUD/ammo/health atlas hashes to `rtx.uiTextures` in both
  `rtx.conf` and `scripts\rtx.conf`. The existing configured-UI shadow path will
  then give those textures valid hashes and Remix will rasterize them as UI.
- If the HUD table is empty but `world_unshadowed_candidate` rows show late
  `frameDrawIndex` with `projOrtho = 1`, the HUD draws keep depth/alpha state
  that the overlay heuristic does not match; widen `IsFixedFunctionOverlayDraw`
  using that evidence rather than guessing.

## 2026-06-02 - HUD Discovery Run: Negative Result and Re-Based Taxonomy

Ran the in-world HUD discovery build, reached gameplay, quit.

What the run showed:

- 89 `hud_candidate` textures were captured and DDS-dumped. A labeled contact
  sheet (`re_docs\hud_candidates_contact_sheet.png`, generated by
  `tools\make_hud_contact_sheet.py`) makes the verdict obvious: the bucket is
  almost entirely world effect sprites - explosions, smoke/muzzle puffs, blood
  splats, bullet-hole decals, glow sprites. No ammo digits, health bars, weapon
  icons, or fonts. So the fixed-function depth-off/alpha-on overlay signature
  isolates particles/decals, not the HUD.
- `projOrtho = 0` for all 89: Total Overdose does not use an orthographic
  projection for these draws. The ortho assumption was wrong; `frameDrawIndex`
  remained the useful signal.
- Extracted gameplay frames (`re_docs\video_frames_200948\frame_004.jpg`,
  `frame_007.jpg`) confirm the 3D world renders correctly under RTX but the
  entire 2D HUD layer is absent.

Re-based diagnosis:

- The in-game HUD is screen-space (RHW) drawn after the 3D world each frame.
  Remix emits `Skipped drawcall, using pre-transformed vertices` for RHW draws
  and only rasterizes them if the texture hash is in `rtx.uiTextures`. The main
  menu was fixed exactly this way; the in-game HUD's hashes are simply not tagged
  yet. So this is a tagging/discovery problem, not a new render bug.
- The clean behavioral discriminator: menu/fullscreen UI is RHW drawn *before*
  any world geometry in the frame; the in-game HUD is RHW drawn *after* it.

Implemented (still additive, read-only):

- Added `g_worldDrawnThisFrame`, set when real fixed-function indexed world
  geometry is drawn (excluding overlay sprites) and reset at `Present`.
- Re-based the candidate taxonomy on draw behavior:
  - `ui_candidate`     - RHW screen-space, before world (menu/fullscreen).
  - `hud_candidate`    - RHW screen-space, after world (in-game HUD overlay).
  - `effect_candidate` - fixed-function overlay sprites (particles/decals).
  - `video_candidate`, `world_unshadowed_candidate` unchanged.
- `hud_candidate` now DDS-dumps the real in-game HUD into
  `rtx-remix\logs\tod-candidate-hud`. The stale effect dumps from the prior build
  were cleared.
- Added a `worldDrawn` TSV column.
- Fixed a data-hygiene bug: the candidate TSV was appended across runs, so its
  first-line header went stale (8 stacked headers, 26..35 columns) and the
  analyzer dropped the new columns. The first row of each process now truncates
  the file and rewrites the header (`CREATE_ALWAYS`); later rows append.
- Analyzer: added `effect_candidate`, corrected the HUD section to "In-Game HUD
  Candidates" (RHW after world).

Build: `build.bat` clean (`/W4`); installed to `scripts\TODCameraResend.asi`.

Next run:

- Launch, reach gameplay so the HUD is on screen, optionally open the pause menu,
  quit.
- Regenerate the report and rebuild the HUD contact sheet:

```powershell
powershell -ExecutionPolicy Bypass -File tools\analyze_remix_candidates.ps1
C:\Python314\python tools\make_hud_contact_sheet.py
```

- The `## In-Game HUD Candidates For Visual Review` table and the
  `tod-candidate-hud` contact sheet should now show real HUD art (ammo/health/
  weapon/radar). Add only those hashes to `rtx.uiTextures` in both config paths.
- If `hud_candidate` is still empty or wrong, inspect `world_unshadowed_candidate`
  rows with `worldDrawn = 1`: the HUD may not be RHW, in which case the next
  discriminator is render-target/late-frame ordering, not vertex layout.

## 2026-06-02 - In-Game HUD Hashes Identified and Tagged

Re-ran with the re-based taxonomy, reached gameplay + pause, quit. The
`hud_candidate` bucket (RHW after world) collapsed from 89 noisy effect rows to
22 clean entries. The contact sheet (`re_docs\hud_candidates_contact_sheet.png`)
matched 1:1 against a reference screenshot of the non-Remix HUD
(`re_docs\reference_hud_no_remix.jpg`): radar/compass dial, health cross, weapon
icon, hand/melee icon, the rewind/Loco chevrons, objective marker, meter bars,
reticle, cursor, and frame/separator pieces. Every one is legitimate screen-space
UI.

The frame-ordering discriminator worked exactly as intended: all 22 had
`worldDrawn = 1` (drawn after the world), and none were the world textures that
polluted the older `ui_candidate` review. `projOrtho = 0` for all, confirming the
HUD uses the perspective projection (the ortho heuristic stays informational
only).

Applied all 22 HUD hashes to `rtx.uiTextures` in both `rtx.conf` and
`scripts\rtx.conf` (now 29 positive + 4 negative entries). No ASI rebuild is
required: the ASI reads `rtx.uiTextures` at startup and already shadows
configured-UI managed textures into the default pool so Remix can hash them, and
Remix rasterizes tagged textures instead of skipping them as pre-transformed.

HUD hash -> element (from contact-sheet review):

```text
0x192FB0F755F9FB6B  radar / compass dial (128x128)
0x30EA8E3DAB2022B6  health cross (32x32)
0x1E236CA8E4A0E146  weapon icon (32x32)
0x161210380A082CA6  hand / melee icon (32x32)
0x4213AE49667A8570  rewind / Loco chevrons (32x32)
0x69E6555960AC4957  objective marker dot (16x16)
0x005DE970A83F42BC  blue meter bar (32x16)
0x3DD8EA530868E561  red meter bar (32x16)
0xACA2948D1DEE9DA6  vertical meter fill (8x32)
0x341023E50A5AF296  target ring (16x16)
0x52E28B6A39EBAF18  reticle ring (64x64)
0x68053536F4B78B31  HUD glow (32x32)
0x382AF0C23A12C318  dot (8x8)
0x733D6B444EF44DE1  ammo / bullet pip (8x16)
0x76CEF1929D90BDC8  bracket frame piece (8x32)
0xBE928CE879426290  bar frame (32x32)
0xA4ADA10820E40A59  horizontal frame bar (16x8)
0x8A3BF58236BAB5EE  vertical frame bar (8x16)
0xF56A533A409D3015  rounded corner piece (16x16)
0xD74088F78FFBED17  cursor arrow (16x32)
0x3F6C008F32FE3D33  white fill square, DXT1 (16x16)
0x093C29F27ECFCF21  black slot square, DXT1 (16x16)
```

Next run (verification):

- Launch, reach gameplay, confirm the HUD now renders (radar, health, ammo,
  weapon, Loco icons).
- If some elements still miss, re-run the analyzer + contact sheet; any remaining
  HUD textures will show as fresh `hud_candidate` rows to add.
- If a tagged texture is wrongly rasterized (e.g. the glow/reticle reads as a flat
  sprite in the world), remove just that hash with a `-0x...` entry.

## 2026-06-02 - HUD Confirmed; Mission 3 Billboard Spikes; Log Cap

User result:

- HUD now renders correctly (radar, health bar, weapon/fist icon, Loco meter).
  The 22 tagged hashes worked.
- Mission 3 shows severe thin vertical "needle/spike" geometry across the distant
  cityscape, and the scene looks flat ("like no ray tracing").

Diagnosis (from `scripts\rtx-remix\logs\remix-dxvk.log`, run 03:13-03:23):

- Raytracing is active, not disabled:
  - `RenderPass GBuffer Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Direct Raytrace Mode: Ray Query (CS)`
  - `RenderPass Integrate Indirect Raytrace Mode: Trace Ray (RGS)`
  The flat look is bright outdoor daylight plus the broken distant geometry; low
  GPU% (16) with 231 FPS is frame/CPU-bound, not RT-off.
- The spikes correlate with one Remix warning:
  - `[RTX] InstanceManager: detected unsupported quad index layout for billboard creation`
- Source trace (`third_party\dxvk-remix\src\dxvk\rtx_render\rtx_instance_manager.cpp`):
  - `createBillboards()` is called (line ~1322) for unordered (alpha-blended),
    non-decal instances when `rtx.enableSeparateUnorderedApproximations` is on.
  - It only accepts quads whose indices follow `A,B,C,A,C,D` (line ~2039). TOD's
    distant foliage/LOD billboards use a different index layout, so Remix aborts
    billboard creation and raytraces the raw geometry. That raw geometry is
    engine-billboarded/degenerate, so as 3D rays it renders as spikes.
- This is a Remix billboard-format limitation with this game's distant LOD, not a
  regression from the HUD work (which only tags 22 small UI textures) and not an
  ASI bug. Missions 2/3 were already noted as fragile earlier in this log.

`rtx.enableSeparateUnorderedApproximations` is about unordered *lighting* of
particles, not geometry, so toggling it is not the fix.

Recommended fix path (not yet applied - needs the offending texture hash):

- Use `rtx.hideInstanceTextures` to hide the spiky foliage/LOD texture(s), the
  same hash-set mechanism as `rtx.uiTextures`. Best identified at runtime via the
  Remix Developer/Game Setup menu by selecting the spiky geometry's texture and
  assigning it the "Hide Instance Textures" category, which writes the hash to
  `rtx.hideInstanceTextures`. Alternatively extend the ASI candidate logger to
  dump alpha/world LOD textures so the hash can be picked offline.

Log hygiene fix (applied):

- The ASI main log (`tod-camera-resend.log`) was 850 MB because every Log() call
  opened it with `OPEN_ALWAYS | FILE_APPEND_DATA`, so it accumulated across all
  runs and never truncated.
- Fixed: the first line of each process truncates the log (`CREATE_ALWAYS`), and a
  128 MB in-process safety cap drops further lines after writing one final notice.
- Build clean (`/W4`); installed to `scripts\TODCameraResend.asi`. The stale
  850 MB log was deleted.

## 2026-06-02 - Mission 3 Regression A/B Test

User clarified the key fact: mission 3 rendered correctly in the past **with
raytracing ON**. That makes the spikes a regression, not the inherent
billboard-with-RT limitation (which would always have spiked with RT on).

The only rendering-affecting change this session was the 22 new `rtx.uiTextures`
HUD hashes. Mechanistically, UI tagging should only make Remix rasterize those
draws (not stretch geometry), and the candidate TSV could not confirm or deny
shared world usage because the ASI's `configuredUi` check short-circuits role
classification once a hash is tagged. So an A/B test is the clean isolator.

Action: reverted `rtx.uiTextures` in both `rtx.conf` and `scripts\rtx.conf` to the
pre-HUD set (7 positive + 4 negative). The full 22-hash HUD list is preserved in
the `2026-06-02 - In-Game HUD Hashes Identified and Tagged` section above for
instant restore. No ASI rebuild required.

Expected outcomes of the next mission-3 run:

- Spikes GONE -> the HUD tags caused the regression. Re-add the HUD hashes minus
  any shared/generic ones (prime suspects: the solid `0x3F6C008F` white and
  `0x093C29F2` black 16x16 DXT1 squares, and possibly the `0x68053536` glow /
  `0x52E28B6A` ring) and re-verify both HUD and mission 3.
- Spikes REMAIN -> the HUD tags are not the cause; restore all 22 (HUD works) and
  pursue the spikes via `rtx.hideInstanceTextures` on the offending LOD texture.

### A/B Result: HUD Tags Exonerated

User ran mission 3 with the 22 tags reverted: the spikes are still present, and the
HUD is faintly visible anyway (expected - the ASI shadows RHW screen-space textures
regardless of UI tagging, so they get hashes and are raytraced even untagged). So
the HUD tags are not the cause. Restored all 22 to both config paths.

This also means nothing this session changed mission-3 rendering: every other change
was logging/discovery (read-only), the `IsBehaviorOverlayDraw` refactor was verified
behavior-preserving, and the camera-resend / RT-redirect / shadow policy were
untouched. So the mission-3 spikes pre-date this session - a Remix distant-LOD
geometry compatibility issue, not a regression from the HUD work. The user recalls
mission 3 looking correct with RT on at some earlier point, which predates the
session (likely an earlier ASI shadow-policy or Remix-config state).

Open: identify and hide the spiky distant-LOD/foliage geometry. Cleanest is the
Remix Developer/Game Setup menu (select the geometry -> "Hide Instance Textures"
category -> writes `rtx.hideInstanceTextures`). Low-effort config shot worth trying
first: `rtx.enableSeparateUnorderedApproximations = False` (gates the billboard
path). Thorough fallback: extend the ASI candidate logger to DDS-dump world/LOD
textures so the culprit hash can be picked offline like the HUD set.

### 2026-06-02: World/LOD candidate discovery added (chosen fix path)

User chose the "discovery" path and set an explicit design constraint: the shim's
end state must be clean - no per-texture/per-content hacks in the C++; anything
game-specific (sky/LOD hashes) belongs in the Remix config, and the discovery
machinery is temporary tooling that must be switchable off for a shipping build.

Implemented accordingly in `TODCameraResend.cpp`:

- New compile-time toggle `kEnableCandidateDiscovery` (subsystem 4). When false,
  `LogTextureCandidateForDraw` early-returns and the whole diagnostic harness
  (TSV + DDS dumps) is inert / dead-stripped, leaving only the three runtime
  subsystems (camera resend, RT redirect, managed-texture shadowing).
- New role `kCandidateRoleWorld` ("world_candidate"): 3D fixed-function world
  geometry. The classifier's tail now tags *all* such draws as world (previously
  only the unshadowed ones were logged, and none were DDS-dumped); the existing
  `world_unshadowed` sub-flag is preserved for the report. World candidates DDS-
  dump to `rtx-remix\logs\tod-candidate-world`.
- `analyze_remix_candidates.ps1`: added a "World LOD Candidates For Visual Review"
  section (groups world_candidate by hash; shows AlphaBlend / ZWrite / PrimType so
  billboard-like draws stand out) and added `world_candidate` to the role loop.
- `make_hud_contact_sheet.py`: hash/dims parse made robust to multi-word role
  prefixes so it can render the world dir too (point argv[1] at tod-candidate-world).

No hash is hardcoded in the binary. The fix itself will be `rtx.hideInstanceTextures`
entries in `rtx.conf` + `scripts\rtx.conf`, chosen by eyeballing the contact sheet.

Procedure: run mission 3 into the spiky area for ~10-20s, quit. Then
`tools\analyze_remix_candidates.ps1` -> report, and
`python tools\make_hud_contact_sheet.py rtx-remix\logs\tod-candidate-world re_docs\world_candidates_contact_sheet.png`
-> contact sheet. Identify the skyline/foliage/impostor atlas, add its hash(es) to
`rtx.hideInstanceTextures` in both config paths, re-verify. Note: capturing all
world textures means the candidate registry fills with many entries, so the
discovery run may hitch slightly until the 2048-row cap is hit - expected, tooling
only.

### 2026-06-02: Mission-3 spikes identified = animated water surface

World/LOD discovery run captured 375 world_candidate textures (436 DDS dumps).
Filtering world_candidate by draw state surfaced an unmistakable signature: a
cluster of **13 distinct 64x64 DXT1 textures, all alpha-blended, each drawn once
at exactly 1576 triangles (788 quads)**. The contact sheet
(`re_docs/world_1576_sheet.png`) shows all 13 are teal caustic / water-surface
frames (`0x193D74DE...` is the calmer deep-water variant). So mission 3's spikes
are the **animated water plane**: a grid of ~788 alpha-blended quads cycling these
caustic frames. Remix classifies the alpha-blended batch as unordered geometry and
tries billboard reconstruction; TOD's quad index layout doesn't match the required
A,B,C,A,C,D, so it aborts and raytraces the raw quads -> spikes over the water.

Fix applied: `rtx.enableSeparateUnorderedApproximations = False` in BOTH config
paths. This disables the billboard/unordered approximation path so Remix raytraces
the water quads as the real geometry they are -> spikes gone AND water stays
visible. Chosen over `rtx.hideInstanceTextures` because hiding these would make the
water disappear entirely (bad for a harbor mission).

Fallback if the toggle regresses other effects or doesn't fully clear the spikes -
hide the water instances instead (spikes gone but water becomes invisible). The 13
water-surface hashes:
  0x146FA9B339F65D31, 0x193D74DE9FEEA9DE, 0x1EDECDE19C4ADE25, 0x2D0077B33D221EAA,
  0x3695A7533A66A4FA, 0x453D95183A84B166, 0x71BE65AC94C04699, 0x71E3A8EF30A7D044,
  0x7E9946863EAD4CA3, 0xCB9BB80E34DE5A71, 0xE4F691C8F8FBC4C6, 0xEEF6D867ECC4A472,
  0xFDC85D13DB312CB8
(add to `rtx.hideInstanceTextures` in both rtx.conf paths if needed.)

AWAITING user mission-3 verification with the toggle.

### 2026-06-02: ROOT CAUSE found via the old working build (regression confirmed)

The `enableSeparateUnorderedApproximations = False` test made no difference, so the
water-billboard finding above was a symptom, not the cause. The user pointed to a
known-good copy at `C:\GOG Games\TotalOverDoseRTXRemix` (dated 2026-06-01) where
mission 3 raytraced correctly.

Diff of that build's config vs the current one - the current build is MISSING:
  rtx.raytracedRenderTarget.enable = True
  rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB, 0x261F0A146E18E6FA,
    0x4E44041F9E27548B, 0x4E44041F9E27548E, 0x572A88A0A2E43086, 0x609466F92E702865,
    0xB566CD7C690A3630, 0xD824D823D9CCF030, 0xF885C65160789A8B

These were removed on the assumption (this log, "Removed the guessed
rtx.raytracedRenderTargetTextures list") that the ASI's RT-redirect made them
redundant. That assumption was wrong for mission 3.

Critical detail: the old working build's ASI (TODCameraResend.cpp, 65867 bytes,
2026-06-01) ALSO has the RT-redirect toggles ON (kRedirectFullSizeSceneRtToPrimary
= true, kRedirectAllColorRtTexturesToPrimary = true). So the two mechanisms COEXIST
- the redirect was never a replacement for raytracedRenderTarget. Mission 3 needs
the native raytracedRenderTarget feature pointed at those 9 RT descriptor hashes;
the redirect alone does not raytrace mission 3's scene (hence "looks like no ray
tracing at all" + the water spikes).

Fix: restored both `rtx.raytracedRenderTarget.enable = True` and the 9-hash
`raytracedRenderTargetTextures` list to BOTH rtx.conf paths. Config-only, no ASI
rebuild (current ASI already has the redirect, matching the working build). The
9 hashes are RT descriptor hashes, NOT game content baked into the binary - they
live in config, consistent with the no-hardcoded-content rule.

AWAITING user mission-3 verification.

### 2026-06-02: Regression isolated to the ASI binary (texture-substitution rewrite)

After restoring both config items, mission 3 was STILL broken with the current ASI.
Deployed the old build's ASI (132608 bytes, 2026-06-01) into scripts\ with the same
restored config -> user confirms "RTX working now on 3rd mission". So the regression
is in the current ASI source, not config. (Current ASI backed up to
tools\TODCameraResend.current.asi.bak.)

Diff of the texture-substitution path, old (working) vs current (regressed):
- WHEN: old substitutes the DEFAULT copy inside Hook_SetTexture (copy stays bound
  across all subsequent draws); current defers substitution to draw time
  (ApplyTextureStageForDraw, called from the 4 draw hooks).
- WHICH STAGES: old substitutes whatever stage the game binds (Hook_SetTexture fires
  per stage); current only substitutes stage 0 (ApplyTextureStateForDraw -> stage 0).
- WHICH TEXTURES: old gates purely by format/dims
  (ShouldCreateDefaultTextureCopy = compressed OR narrow UI-dim A8R8G8B8 whitelist);
  current adds a draw-layout gate (ShouldShadowTextureForDraw: compressed always,
  configuredUI always, uncompressed only if overlay-behavior).
Any of these can leave a mission-3 scene draw's managed texture unhashed -> Remix
skips the drawcall -> scene not raytraced. Exact sub-cause not yet pinned; stage-0-
only and the deferred timing are the leading suspects for world geometry.

Decision pending HUD check on the old ASI: if the old ASI also renders the HUD
(its whitelist covers the HUD dims and the 22 uiTextures tags are still in config),
the old source becomes the canonical base and the clean-code work (kEnableCandidate
Discovery toggle, header docs, world_candidate role) gets re-applied on top WITHOUT
changing runtime behavior. The current source's substitution rewrite is the
regression and must not be the base.

### 2026-06-02: Lean rebuild on the mission-3-working ASI base

User checked the old ASI with mission 3 working: HUD was only partial (some messages
and one infinity HUD item), and RTX still dropped at specific view angles. Therefore:
- Mission-3 "no RTX/glitchy" regression is confirmed to be from the later ASI's
  texture-substitution rewrite.
- Partial HUD is a separate issue: the old ASI's one-time DEFAULT copies are likely
  stale/blank for some managed UI atlases, or not explicitly classified through
  `rtx.uiTextures`.
- Angle-dependent RTX dropout exists on the old working ASI too, so it predates the
  later rewrite and should be handled after the base is stable.

Action taken:
- Replaced the current `tools\tod_camera_resend_asi\TODCameraResend.cpp` with the
  old mission-3-working source from `C:\GOG Games\TotalOverDoseRTXRemix`.
- Backed up the regressed source as
  `tools\tod_camera_resend_asi\TODCameraResend.regressed_source_20260602.cpp.bak`.
- Kept the old, proven `Hook_SetTexture` architecture: substitution happens when
  the game binds a texture, and applies to every stage the game binds. The regressed
  draw-time/stage-0-only substitution path was NOT reintroduced.
- Added minimal config-driven UI support only:
  - parse `rtx.uiTextures` from both `rtx.conf` and `scripts\rtx.conf`;
  - compute the Remix-style XXH3 level-0 texture hash lazily;
  - allow extra managed texture copies only when their hash is in `rtx.uiTextures`;
  - negative `rtx.uiTextures` entries remove hashes from the local match set;
  - refresh uncompressed UI-like/configured DEFAULT copies on bind so dynamic HUD
    atlases do not stay blank.
- No candidate discovery, DDS dumping, world-role scanning, or per-mission texture
  hashes are present in the rebuilt source.

Build result:
- `cmd /c tools\tod_camera_resend_asi\build.bat` succeeded.
- New live ASI: `scripts\TODCameraResend.asi`, 151552 bytes,
  timestamp `2026-06-02 19:19:06`.
- Regressed 167936-byte ASI remains backed up at `tools\TODCameraResend.current.asi.bak`.

Next verification:
1. Launch game fresh.
2. Check mission 3: RTX should behave like the old ASI baseline (working, though the
   known angle-dependent dropout may still exist).
3. Check HUD/main/pause/menu text: the targeted UI refresh/hash path should improve
   beyond the old partial HUD without re-breaking mission 3.

### 2026-06-02: Lean UI rebuild failed - restored exact known-good baseline

User tested the 151552-byte lean UI rebuild and reported mission 3 regressed again:
"Nah the glitch is back... no rtx". This means even the scoped UI/hash/refresh
changes are unsafe for the mission-3 renderer. The failure is not only the earlier
large draw-time/stage-0 rewrite; the game/Remix combo is sensitive enough that
runtime texture locking/hash reads or UI copy refreshes can also disturb the scene
RT path.

Reset performed:
- Backed up the failed ASI as `tools\TODCameraResend.lean_ui_attempt_20260602.asi.bak`
  (151552 bytes, timestamp 2026-06-02 19:19:06).
- Restored the exact known-good ASI from
  `C:\GOG Games\TotalOverDoseRTXRemix\scripts\TODCameraResend.asi` into
  `scripts\TODCameraResend.asi` (132608 bytes, timestamp 2026-06-01 06:27:35).
- Backed up the failed source as
  `tools\tod_camera_resend_asi\TODCameraResend.lean_ui_attempt_20260602.cpp.bak`
  (79067 bytes, timestamp 2026-06-02 19:18:55).
- Restored the exact known-good source from
  `C:\GOG Games\TotalOverDoseRTXRemix\tools\tod_camera_resend_asi\TODCameraResend.cpp`
  into `tools\tod_camera_resend_asi\TODCameraResend.cpp` (65867 bytes,
  timestamp 2026-06-01 06:27:30).

Current rule:
- Treat the exact old ASI as the protected RTX baseline.
- Do NOT add runtime texture hashing, candidate/discovery LockRect reads, UI copy
  refreshes, draw-time substitution, candidate discovery, or broader texture
  shadowing to the shipping ASI until the angle-dependent RTX dropout is
  understood. The old ASI's generic managed-texture DEFAULT-copy path already
  performs the only accepted texture locks.
- HUD work must proceed through non-invasive config/capture analysis first, or via
  an isolated A/B build that is immediately reverted if mission 3 loses RTX.

Next verification:
1. Launch fresh and confirm mission 3 RTX is back to the reference baseline.
2. After that, investigate HUD/menu text without modifying the live ASI.

### 2026-06-02: Ghidra-backed support boundary documented

Created `re_docs\rtx_remix_ghidra_support_design.md` to reset the investigation
around the actual `TOD.exe` renderer instead of trial-and-error texture behavior.

Key confirmed facts:
- `FUN_00421530` is the main render-entry loop; it sets projection every entry,
  sets view only when the camera matrix changes, then flushes renderlists.
- `FUN_004342c0` is the renderlist interpreter.
- Static model geometry is:
  `FUN_00884eb0 -> opcode 0x11 -> FUN_004540e0 -> DrawIndexedPrimitive`.
- Alternate model geometry is:
  `opcode 0x12 -> FUN_00454660 -> DrawIndexedPrimitive`.
- `FUN_0044def0`, `FUN_0044e400`, and `FUN_0044e580` submit D3D fixed-function
  world/view/projection transforms.
- `FUN_0044e220` centralizes `SetRenderTarget` / `SetDepthStencilSurface`.
- `FUN_0044f8a0` / `FUN_004634b0` centralize engine `SetTexture`.

Implication:
- TOD is already a normal fixed-function indexed D3D9 renderer on the world path.
- Proper Remix support is render-target + camera-pass correctness, not texture
  readback/hashing/refresh in the ASI.
- The protected baseline ASI remains live. Future work should inspect the
  angle-dependent mission-3 dropout via RT/camera state only, then update
  `rtx.raytracedRenderTargetTextures` or add an engine-aware camera resend around
  world renderlist opcodes if proven necessary.

### 2026-06-02: Clean runtime ASI built from Ghidra-backed boundary

User asked to stop trial-and-error texture logic and do the clean/proper route.
Action taken:

- Kept the protected known-good source as the base.
- Backed up the old live binary/source:
  - `tools\TODCameraResend.known_good_before_clean_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.known_good_before_clean_20260602.cpp`
- Added `TOD_ENABLE_RUNTIME_LOG = 0` and compiled runtime logging out of the ASI.
- Preserved the only proven generic runtime subsystems:
  - fixed-function camera cache/resend;
  - texture-backed RT redirect and stale RT-composite skip;
  - managed texture DEFAULT-pool copy at `SetTexture` time.
- Did not add HUD hash reads, UI refresh, candidate discovery, DDS dumping,
  per-mission hashes, or draw-time texture substitution. The only remaining
  texture locks are the known-good managed-texture DEFAULT-copy path.
- Cleaned both `rtx.conf` files so root and `scripts` copies are identical.
- Verified both `dxvk.conf` files are identical.

Build result:

```text
cmd /c tools\tod_camera_resend_asi\build.bat
result: success, /W4 clean
live ASI: scripts\TODCameraResend.asi
size: 119296 bytes
sha256: 96C9EB6559DC67C38DF1EDB63CD6C53A010F7EC3D6D9...
```

Ghidra confirmation added during this pass:

- `SetTransform` call sites are still only world/view/projection/texture transforms.
- `FUN_0044e400` view submission has one main render-loop caller.
- `FUN_0044e580` projection submission has multiple valid callers, so a single
  engine inline hook is not the cleanest base.
- `FUN_0044e220` centralizes render-target switching.

Next test should be mission 3 with this clean build. If RTX still drops at specific
angles, the next instrumented build must be RT/camera-only and temporary.

### 2026-06-02: Temporary missing-texture metadata diagnostic

User tested the clean build and reported:
- RTX appears active (sun/glow still works);
- some textures/player visibility disappear at certain angles.

Fresh log location was `scripts\rtx-remix\logs\remix-dxvk.log`, not the root log
folder. The run showed:

```text
[RTX-Compatibility-Info] Texture 0 without valid hash detected, skipping drawcall.
RenderPass GBuffer Raytrace Mode: Ray Query (CS)
RenderPass Integrate Direct Raytrace Mode: Ray Query (CS)
RenderPass Integrate Indirect Raytrace Mode: Trace Ray (RGS)
Camera cut detected on frame 627 / 669 / 671
Attempted invert a non-invertible matrix.
```

Interpretation:
- This is not a full RTX-off state; raytracing starts and stays active.
- The disappearing-player/texture symptom is most likely a skipped draw due to a
  stage-0 texture that Remix still cannot hash, or a camera-cut/matrix edge case.
- Since the active warning is `Texture 0 without valid hash`, the first diagnostic
  target is metadata only for fixed-function indexed world draws whose stage-0
  texture remains a managed/original texture at draw time.

Temporary build:
- Backed up clean runtime ASI:
  `tools\TODCameraResend.clean_runtime_20260602.asi` (119296 bytes).
- Built temporary diagnostic ASI:
  `scripts\TODCameraResend.asi` (123392 bytes, timestamp 2026-06-02 19:52:31).
- Added `TOD_ENABLE_MISSING_TEXTURE_DIAG = 1`.
- Diagnostic output path:
  `rtx-remix\logs\tod-missing-texture-diag.log`.

Diagnostic constraints:
- No texture pixel reads beyond the existing known-good managed DEFAULT-copy path.
- No hash computation.
- No DDS dumping.
- No HUD/UI refresh.
- No rendering behavior change; logs only metadata:
  width, height, format, usage, pool, mip levels, current RT, FVF/declaration.

Next run:
1. Launch mission 3.
2. Move to the angle where player/textures disappear.
3. Quit.
4. Analyze `tod-missing-texture-diag.log` and fresh `scripts\rtx-remix\logs\remix-dxvk.log`.
5. Restore or rebuild clean ASI after extracting the evidence.

### 2026-06-02: Missing-texture diagnostic result; clean ASI restored

User could not reproduce the angle-specific player disappearance. During the
temporary diagnostic run, a new minor texture flicker appeared while driving. This
may have been caused by the diagnostic build itself because it wrote ~1.1 MB of
draw metadata during movement-heavy gameplay.

Action:
- Restored/rebuilt the clean runtime ASI.
- Removed the temporary missing-texture diagnostic code from source.
- Live ASI:
  `scripts\TODCameraResend.asi`, 119296 bytes, timestamp 2026-06-02 20:00:38.
- Source scan confirms no `TOD_ENABLE_MISSING_TEXTURE_DIAG`, `DiagnosticLog`, or
  `tod-missing-texture-diag` code remains.

Fresh Remix log still shows RTX active:

```text
Texture 0 without valid hash detected, skipping drawcall.
RenderPass GBuffer Raytrace Mode: Ray Query (CS)
RenderPass Integrate Direct Raytrace Mode: Ray Query (CS)
Camera cut detected on frame 631 / 723 / 725
```

Diagnostic finding:
- Missing-hash candidates are overwhelmingly managed `A8R8G8B8` textures on real
  fixed-function indexed draws to the primary RT:
  - `128x128/A8R8G8B8`: 1172 sampled rows
  - `64x64/A8R8G8B8`: 980
  - `32x32/A8R8G8B8`: 507
  - `32x16/A8R8G8B8`: 222
  - `128x32/A8R8G8B8`: 220
  - `64x32/A8R8G8B8`: 206
- Layout distribution:
  - `FVF 0x112` dominates.
  - `FVF 0x152` also appears.
  - some declaration-path draws appear.
- These are not a camera/RT failure. They are the known remaining managed A8 hash
  gap.

Important design implication:
- The HUD is still absent because the clean ASI intentionally avoids broad A8
  shadowing. The earlier broad A8/default-shadow paths made some HUD/text appear,
  but also corrupted mission lighting, sky, vehicles, or texture assignment.
- A dimension whitelist is still rejected as unclean.
- The next proper HUD/A8 fix must distinguish static albedo/UI textures from
  dynamic light/effect/video/mask textures using engine/material behavior or robust
  managed-texture dirty/lifecycle semantics, not by hardcoded texture sizes.

Next required user check:
- Retest mission 3/driving with the clean 119296-byte ASI now that the diagnostic
  writer is gone.
- If flicker remains, investigate A8 managed texture handling.
- If flicker disappears, treat it as diagnostic overhead and continue HUD work
  separately.

### 2026-06-02: Clean config-owned HUD support restored

User asked to proceed with the cleanest HUD path and noted that the earlier
Claude build made UI work by finding textures. Re-reviewed the Ghidra-backed
design and the prior HUD candidate results.

Static RE check:
- TOD's `place_in_hud` property is a real model flag, not a texture-size hint.
- Disassembly around the registration labels shows:
  - setter `00883dc0`: writes bit `0x8000` at model/object offset `0x6c`;
  - getter `00883df0`: reads that same bit.
- The current ASI operates at the D3D9 device boundary and does not receive TOD
  model-object pointers. Using `place_in_hud` directly would require a new inline
  engine hook into model/renderlist emission, which is more invasive than needed
  for the already-confirmed HUD texture set.

Clean runtime decision:
- Keep the known-good `SetTexture`-time substitution architecture. Ghidra confirms
  TOD centralizes texture flushing through `FUN_0044f8a0 -> FUN_004634b0` and only
  tracks two texture stages there.
- Remove the uncompressed A8 dimension whitelist as a runtime content decision.
- Preserve generic compressed managed texture copying, because that is the
  proven world/Remix compatibility path.
- For uncompressed managed textures, create/bind a DEFAULT-pool copy only when a
  one-time RTX hash of the source texture exactly matches `rtx.uiTextures`.
- Do not add LockRect/UnlockRect/AddDirtyRect hooks, dirty invalidation, refresh
  on bind, candidate logging, DDS dumping, or mission-specific/hash-hardcoded C++.
  The earlier 151 KB lean UI attempt failed because it mixed config matching with
  runtime refresh/dirty tracking.

Build result:
- Source: `tools\tod_camera_resend_asi\TODCameraResend.cpp`
- Live ASI: `scripts\TODCameraResend.asi`
- Size: `136704` bytes
- Timestamp: `2026-06-02 20:11:13`
- Backups:
  - `tools\TODCameraResend.config_ui_runtime_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.config_ui_runtime_20260602.cpp.bak`
- Build command: `cmd /c tools\tod_camera_resend_asi\build.bat`
- Build status: success, `/W4` clean.

Config state preserved:
- `rtx.raytracedRenderTarget.enable = True` in both configs.
- The 9 `rtx.raytracedRenderTargetTextures` hashes remain in both configs.
- The 22 confirmed HUD hashes remain in `rtx.uiTextures` in both configs.
- `d3d9.evictManagedOnUnlock = True` remains in both dxvk configs.

Expected behavior:
- Mission 3 should keep the known-good RTX path because the ASI still binds
  managed texture copies at `SetTexture` time for every stage and does not use
  the later draw-time stage-0 rewrite.
- HUD/menu textures that are explicitly listed in `rtx.uiTextures` can now get
  hashable DEFAULT-pool copies without broad A8 world/light/effect copying.
- If a HUD element is still missing, the next clean step is to add its confirmed
  hash to config, not to widen ASI heuristics by size.

Result:
- User reported: "RTX gone screen glitch back".
- Rejected this approach. Even one-time runtime hash probing of uncompressed
  managed textures is not safe for TOD's render path. It can disturb the same
  managed A8 path that mission 3 depends on, despite avoiding dirty-refresh
  hooks.

Rollback:
- Restored live ASI from `tools\TODCameraResend.clean_runtime_20260602.asi`.
- Active live ASI:
  `scripts\TODCameraResend.asi`, `119296` bytes, timestamp `2026-06-02 19:42:49`.
- Restored active source from
  `tools\tod_camera_resend_asi\TODCameraResend.known_good_before_clean_20260602.cpp`.
- Source scan confirms no `XXH`, `rtx.uiTextures` parser, configured-UI hash
  matching, diagnostic logger, or dirty/refresh hook remains.

Conclusion:
- Do not reintroduce runtime RTX hash computation for HUD support.
- `rtx.uiTextures` hashes are valid Remix config data, but this ASI cannot safely
  discover/match them by locking arbitrary managed A8 textures at runtime.
- The next clean HUD path must be engine-aware, not texture-content probing:
  inspect TOD model/renderlist/material state around `place_in_hud` and the
  texture wrapper path, then make only that specific engine overlay path hashable
  or rasterized.

Verification:
- User relaunched after rollback and confirmed: "yup its back".
- Treat the 119296-byte clean ASI as the protected live baseline again.

## Pretransformed-After-World A8 UI Bind Test - 2026-06-02 20:39 +05:30

Goal:
- Keep the restored 119296-byte known-good ASI behavior as the baseline for
  mission RTX.
- Add a narrow HUD/menu support test without runtime RTX hashing, config
  parsing, DDS dumping, dirty tracking, or broad managed-A8 copying.

Rationale:
- Prior HUD candidate analysis showed the real HUD as pretransformed
  RHW/POSITIONT screen-space draws that occur after world rendering has begun
  (`worldDrawn=1`) and draw to the primary render target.
- The rejected 136704-byte config-owned hash build proved that locking/hashing
  arbitrary managed A8 textures in the shipping ASI can regress mission RTX.
- This test therefore uses D3D draw semantics only:
  - a non-pretransformed draw to the primary render target marks world rendering
    as begun for the frame;
  - only after that mark, a pretransformed draw on the primary target with a
    managed regular `A8R8G8B8` stage-0 texture can receive a temporary
    DEFAULT-pool copy;
  - the original texture is restored immediately after that one draw;
  - offscreen render-target work does not enable the gate;
  - runtime logging is compiled out with `TOD_ENABLE_RUNTIME_LOG 0`.
- The protected known-good source still contains its old one-mip A8 whitelist.
  This test does not expand that whitelist and does not make dimension matching
  the HUD strategy.

Build result:
- Source: `tools\tod_camera_resend_asi\TODCameraResend.cpp`
- Live ASI: `scripts\TODCameraResend.asi`
- Size: `119808` bytes
- Timestamp: `2026-06-02 20:39:22`
- Backups:
  - `tools\TODCameraResend.pretransformed_a8_after_world_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.pretransformed_a8_after_world_20260602.cpp.bak`
- Build command: `cmd /c tools\tod_camera_resend_asi\build.bat`
- Build status: success, `/W4` clean.

Source scan:
- No `XXH`.
- No runtime `rtx.uiTextures` parser.
- No configured-UI hash matching.
- No `AddDirtyRect`, dirty-refresh path, candidate dump, or diagnostic DDS
  discovery code.

Expected test:
- Mission RTX should remain like the protected rollback baseline because the
  world/RT path is unchanged.
- HUD/menu elements that are pretransformed A8 draws after world rendering may
  become hashable to Remix.
- If mission RTX regresses, roll back to
  `tools\TODCameraResend.clean_runtime_20260602.asi` and reject this draw-state
  gate as still too broad.

Verification:
- User reported the HUD is visible.
- Remaining issue: angle-dependent geometry/texture instability is still present.
  The textures do not simply disappear; at some camera angles geometry appears
  displaced elsewhere, including the player becoming huge/far near the sun.
- Preserve this as `state2`: "RTX back + HUD visible, but angle-dependent
  displaced-geometry glitch remains."

Archive:
- Existing `C:\GOG Games\TotalOverDoseRTXRemix` snapshot moved under
  `C:\GOG Games\TotalOverDoseRTXRemix\state1`.
- This build/state saved under
  `C:\GOG Games\TotalOverDoseRTXRemix\state2`.

## Angle-Dependent Geometry Glitch: Identity View Guard Test - 2026-06-02 21:15 +05:30

Evidence:
- User provided recording:
  `C:\Users\utkar\Videos\NVIDIA\Total Overdose\Total Overdose 2026.06.02 - 20.44.23.03.mp4`.
- Extracted review frames to:
  `re_docs\glitch_204423_frames`.
- Contact sheet: `re_docs\glitch_204423_frames\contact_1fps.jpg`.
- Tight 12s-18s burst sheet:
  `re_docs\glitch_204423_frames\burst_12_18_sheet.jpg`.
- The visible failure window is around the low-angle view toward the bright
  sun/billboard area. The player silhouette grows large with camera angle; the
  remaining unstable look is more consistent with transform/render-state
  desync than with missing texture hashes.

Hypothesis:
- The active state2 source had lost the identity-view resend guard documented
  in the earlier 2026-06-01 runtime notes.
- TOD intentionally sets `D3DTS_VIEW = identity` for some overlay/special passes.
  The ASI was ignoring identity view for camera caching, but still resubmitted
  the cached 3D view/projection before later non-pretransformed fixed-function
  indexed draws.
- That can force a 3D camera onto a pass whose current D3D state is supposed to
  remain identity, causing angle-dependent projected/displaced geometry.

Change:
- Added `g_currentViewIsIdentity` tracking.
- `Hook_SetTransform(D3DTS_VIEW)` now records identity vs non-identity current
  view state.
- `ResendCameraForDraw()` now skips camera resend while the current view is
  identity.
- No texture-copy, HUD bind, hash, config, dirty-refresh, or candidate-dump code
  changed.
- Runtime logging remains compiled out with `TOD_ENABLE_RUNTIME_LOG 0`.

Build result:
- Live ASI: `scripts\TODCameraResend.asi`
- Size: `120320` bytes
- Timestamp: `2026-06-02 21:15:52`
- Backup:
  - `tools\TODCameraResend.identity_view_guard_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.identity_view_guard_20260602.cpp.bak`
- Rollback to prior HUD-visible state:
  `tools\TODCameraResend.pretransformed_a8_after_world_20260602.asi`.

Expected test:
- RTX should remain visible.
- HUD should remain visible.
- The low-angle/sun-facing displaced-geometry flicker may improve if it was
  caused by forced camera resend during identity-view passes.
- If RTX or HUD regresses, restore the prior HUD-visible ASI backup and reject
  this guard for the current branch.

Verification:
- User reported this build works.
- Preserve this as `state3`: "RTX visible + HUD visible + identity-view guard
  improves/fixes the observed angle-dependent displaced-geometry glitch."

## Static Texture Mismatch: Copy Reuse Reset Test - 2026-06-02 21:58 +05:30

Evidence:
- User provided RTX/non-RTX comparison screenshots in:
  `C:\GOG Games\Total Overdose\screenshots\mismatch`.
- `1.png` vs `1.rtx.png`: the trailer mesh has the correct metal material
  without RTX, but RTX shows a numeric/debug-looking texture on the same trailer.
- `2.png` vs `2.rtx.png`: large warehouse/static surfaces differ broadly
  between non-RTX and RTX. This is a material/texture mismatch, not the
  previously fixed identity-view geometry glitch.
- `rtx-remix\mods` and `scripts\rtx-remix\mods` are empty, so this is not a
  Remix replacement-asset/mod override problem.

Hypothesis:
- The ASI caches DEFAULT-pool copies by raw `IDirect3DBaseTexture9*`.
- The current source did not invalidate a cached copy if D3D later reused the
  same COM pointer address for a newly created texture.
- If pointer reuse happens, the ASI can bind a stale DEFAULT copy for a new
  texture object, producing the observed "correct mesh, wrong material" result.

Change:
- Added `ResetTextureCopyForTexture(texture)`.
- `RegisterTexture()` now resets/releases any cached DEFAULT copy for a texture
  pointer when that pointer is registered again.
- This is lifetime/cache hygiene only. No hash parsing, texture-content probing,
  dirty refresh, candidate dumping, HUD gate changes, or Remix config changes.

Build result:
- Live ASI: `scripts\TODCameraResend.asi`
- Size: `120320` bytes
- Timestamp: `2026-06-02 21:58:54`
- Backup:
  - `tools\TODCameraResend.texture_copy_reuse_reset_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.texture_copy_reuse_reset_20260602.cpp.bak`
- Rollback to verified state3:
  `tools\TODCameraResend.identity_view_guard_20260602.asi`.

Expected test:
- RTX should remain visible.
- HUD should remain visible.
- The trailer/warehouse/static material mismatch may improve if stale DEFAULT
  texture-copy reuse was the cause.
- If wrong materials remain, the next likely cause is same-object texture content
  updates after first bind, not pointer reuse; investigate without reintroducing
  broad dirty-refresh hooks.

## Missing Shooting Sparks/Blood: Alpha Effect A8 Test - 2026-06-02 22:21 +05:30

User report:
- Shooting sparks and blood are not visible.

Prior evidence:
- The old candidate report has an `effect_candidate` bucket with many
  `A8R8G8B8` mipmapped textures (`32x32`, `64x64`, `128x128`, etc.).
- The 2026-06-02 discovery note identified that bucket as world effect sprites:
  explosions, smoke/muzzle puffs, blood splats, bullet-hole decals, and glow
  sprites.
- Current runtime support only made compressed textures and HUD-like A8 draws
  hashable. Mipmapped A8 effect sprites were still left as managed textures
  without a Remix-valid DEFAULT-pool copy.

Change:
- Added `IDirect3DDevice9::SetRenderState` hook at vtable index `57`.
- Tracks:
  - `D3DRS_ZENABLE`
  - `D3DRS_ZWRITEENABLE`
  - `D3DRS_ALPHABLENDENABLE`
  - `D3DRS_ALPHATESTENABLE`
- Added a narrow alpha-effect A8 gate:
  - primary render target;
  - fixed-function, non-RHW/non-POSITIONT draw;
  - no vertex shader;
  - alpha blend or alpha test enabled;
  - depth disabled or depth write disabled;
  - stage 0 is managed regular `A8R8G8B8`.
- When the gate matches, the ASI temporarily binds a DEFAULT-pool copy for that
  draw, then restores the original stage-0 texture immediately after the draw.
- No content hashes, no `rtx.uiTextures` parsing, no dirty-refresh hooks, no
  candidate dumps, and no dimension expansion.

Build result:
- Live ASI: `scripts\TODCameraResend.asi`
- Size: `120832` bytes
- Timestamp: `2026-06-02 22:21:56`
- Backup:
  - `tools\TODCameraResend.alpha_effect_a8_20260602.asi`
  - `tools\tod_camera_resend_asi\TODCameraResend.alpha_effect_a8_20260602.cpp.bak`
- Rollback to previous material-cache test:
  `tools\TODCameraResend.texture_copy_reuse_reset_20260602.asi`.
- Rollback to verified state3:
  `tools\TODCameraResend.identity_view_guard_20260602.asi`.

Expected test:
- RTX remains visible.
- HUD remains visible.
- Shooting muzzle/spark sprites and blood splats become visible.
- If world materials become worse, roll back and tighten the alpha-effect gate
  further using fresh evidence rather than widening texture heuristics.

Verification:
- User reported shooting sparks/blood are fixed.
- Remaining issue: some objects/textures appear invisible, but still have
  collision/destruction behavior; when shot, the destruction animation is
  visible. This suggests the base/static draw is missing while the effect or
  destruction pass is now visible.
- Preserve this as `state4`: "RTX visible + HUD visible + angle glitch fixed +
  sparks/blood visible; remaining invisible-destructible objects issue."
