# Game Reverse-Engineering Outputs

Purpose: compact factual outputs only. Experiment history and decisions live in [02_work_log_what_worked.md](02_work_log_what_worked.md). Full original notes are archived under [archive/source_notes](archive/source_notes).

Current target runtime checked on 2026-06-08 in `C:\GOG Games\TotalOverDoseRTXRemix` uses `scripts/TODRemixShim.asi`, `scripts/TODRemixShim.ini`, `scripts/rtx.conf`, and `scripts/dxvk.conf`. Older notes mostly say `TODCameraResend`; that code path was folded into the current `TODRemixShim`.

## Hook Rebuild Specification

This section is the implementation blueprint. The source remains the byte-exact authority, but the following is the minimum RE/hook surface needed to rebuild the shim behavior without reading the archived debug log.

Build target:

- x86 Windows ASI DLL.
- Source path: `tools/tod_remix_shim/TODRemixShim.cpp`.
- Output path: `scripts/TODRemixShim.asi`.
- Build command: `tools/tod_remix_shim/build.bat`.
- Compiler mode from `build.bat`: MSVC x86, C++17, `/W4 /O2 /GR- /MT /LD`.
- Required includes: Windows SDK `windows.h`, `d3d9.h`, and dxvk-remix xxHash header at `third_party/dxvk-remix/src/util/xxHash/xxhash.h`.
- Runtime config path: `scripts/TODRemixShim.ini`.
- Main log path when runtime logging is enabled: `rtx-remix/logs/tod-remix-shim.log`.
- `TOD_ENABLE_RUNTIME_LOG` is currently `0`, so most `Log(...)` calls compile out.

Bootstrap:

```text
DllMain(PROCESS_ATTACH)
  -> InitializePaths(module)
  -> DisableThreadLibraryCalls(module)
  -> CreateThread(WorkerThread)

WorkerThread
  -> poll up to 3000 times, 10 ms each, for d3d9.dll
  -> GetProcAddress("Direct3DCreate9")
  -> create a probe IDirect3D9 through Direct3DCreate9(D3D_SDK_VERSION)
  -> hook IDirect3D9::CreateDevice vtable slot 16
  -> release probe object

Hook_CreateDevice
  -> call original CreateDevice
  -> on success call HookDevice(returnedDevice)
```

D3D9 vtable hooks:

| Interface | Slot | Hook | Purpose |
|---|---:|---|---|
| `IDirect3D9` | 16 | `Hook_CreateDevice` | Install device hooks on the created D3D9 device. |
| `IDirect3DDevice9` | 17 | `Hook_Present` | Per-frame maintenance: patch far clip, install scene update hook, process sky/asset queues, reset `worldDrawnThisFrame`; injected sun path exists but is compiled off in this package. |
| `IDirect3DDevice9` | 23 | `Hook_CreateTexture` | Register texture metadata; hook texture `Release`; register level-0 surface for render-target textures. |
| `IDirect3DDevice9` | 28 | `Hook_CreateRenderTarget` | Register render-target surface metadata. |
| `IDirect3DDevice9` | 34 | `Hook_StretchRect` | Track/log RT copies; behavior passthrough. |
| `IDirect3DDevice9` | 37 | `Hook_SetRenderTarget` | Track RT0/depth state; redirect selected color RT texture surfaces to the primary RT. |
| `IDirect3DDevice9` | 39 | `Hook_SetDepthStencilSurface` | Track current depth-stencil surface. |
| `IDirect3DDevice9` | 44 | `Hook_SetTransform` | Cache non-identity view, projection, and world matrices; ignore identity view for camera resend state. |
| `IDirect3DDevice9` | 57 | `Hook_SetRenderState` | Track depth/alpha states; optional fog-state logger. |
| `IDirect3DDevice9` | 65 | `Hook_SetTexture` | Bind DEFAULT-pool copies for supported managed textures; update texture0/source state; queue sky and forced texture probes. |
| `IDirect3DDevice9` | 69 | `Hook_SetSamplerState` | Apply sampler quality overrides before forwarding. |
| `IDirect3DDevice9` | 81 | `Hook_DrawPrimitive` | Classify pretransformed draws; skip RT composites when configured; bind A8 copies; optional sun auto-aim path is compiled off; mark world draws. |
| `IDirect3DDevice9` | 82 | `Hook_DrawIndexedPrimitive` | Camera resend; sky viewport marker; optional sun auto-aim path is compiled off; optional billboard/freeze diagnostics; mark world draws. |
| `IDirect3DDevice9` | 83 | `Hook_DrawPrimitiveUP` | A8 copy binding; optional sun auto-aim path is compiled off; mark world draws. |
| `IDirect3DDevice9` | 84 | `Hook_DrawIndexedPrimitiveUP` | Camera resend; A8 copy binding; optional sun auto-aim path is compiled off; mark world draws. |
| `IDirect3DDevice9` | 86 | `Hook_CreateVertexDeclaration` | Detect and register declarations with `POSITION` / `POSITIONT`. |
| `IDirect3DDevice9` | 87 | `Hook_SetVertexDeclaration` | Track current declaration and pretransformed state. |
| `IDirect3DDevice9` | 89 | `Hook_SetFVF` | Track current FVF and RHW state. |
| `IDirect3DDevice9` | 92 | `Hook_SetVertexShader` | Track whether a vertex shader is active. |
| `IDirect3DTexture9` | 2 | `Hook_TextureRelease` | Unregister texture and clear cached DEFAULT-pool copies on release. |

D3D vtable hook method:

- Treat object as `void***`.
- Patch `vtable[index]` after `VirtualProtect(..., PAGE_EXECUTE_READWRITE)`.
- Store the original function pointer the first time the hook is installed.
- Restore page protection and flush instruction cache.

TOD inline hooks:

| Target | VA | RVA | Patch bytes | Prologue guard | Hook | Purpose |
|---|---:|---:|---:|---|---|---|
| `SkyBox::Render` | `0x008F1E10` | `0x004F1E10` | 6 | `81 EC` | `Hook_TodSkyBoxRender` | Enter/exit SkyBox scope. |
| `RenderList::SetMaterial` | `0x00431660` | `0x00031660` | 7 | `56 57 8B F9` | `Hook_TodRenderListSetMaterial` | During SkyBox scope, queue material texture evidence. |
| `RenderList::AddMesh` / opcode `0x11` builder | `0x00432C70` | `0x00032C70` | 7 | `8B 41 20 56 8D` | `Hook_TodRenderListAddMesh` | During SkyBox scope, record sky mesh pointers. |
| `RenderMesh::Draw` / opcode `0x11` flush | `0x004540E0` | `0x000540E0` | 9 | `83 EC 34 56 57 8B 7C` | `Hook_TodRenderMeshDraw` | Raise draw-depth while a recorded SkyBox mesh executes. |
| `Node::SetTraverseDistance` | `0x00500BF0` | `0x00100BF0` | 5 | `8B 49 30 85 C9` | `Hook_TodSetTraverseDistance` | Historical LOD diagnostic; current hook is passthrough. |

TOD inline hook method:

- `kTodImageBaseVa = 0x00400000`.
- Resolve target by `GetModuleHandle(nullptr) + RVA`.
- Validate committed executable memory and expected prologue.
- Allocate executable trampoline of `patchBytes + 6`.
- Copy original bytes into the trampoline.
- Append an absolute `push imm32; ret` jump back to `target + patchBytes`.
- If `patchBytes == 5`, patch target with relative `E9`; otherwise patch with `push imm32; ret` and NOP remaining bytes.
- Flush instruction cache.

TOD scene update hook:

- `Scene::Update` VA: `0x00897450`, RVA `0x00497450`.
- Resolve the scene object from `TodGlobalPointer(kTodCameraSystemGlobalRva)` in current source.
- Scan first 64 vtable slots for a pointer equal to `moduleBase + kTodSceneUpdateRva`.
- Hook that slot with `Hook_TodSceneUpdate`.
- Used only when asset crawler support is active/needed.

Required TOD addresses and globals:

| Name | VA | RVA | Use |
|---|---:|---:|---|
| Image base | `0x00400000` | `0x00000000` | RVA base. |
| `LoadNativeResource` | `0x00878AB0` | `0x00478AB0` | Historical/diagnostic sky asset load probe. |
| `Texture::DrawAllTextures()` | `0x00463850` | `0x00063850` | Forced texture/asset capture. |
| `Script::LodFactor` | `0x00A12090` | `0x00612090` | Asset capture LOD cycling; unused render-distance override function still exists but is not called. |
| `AssetManager` global | `0x00A3D7C4` | `0x0063D7C4` | AssetBlock crawler registry access. |
| `Folder` type global | `0x00A3D810` | `0x0063D810` | AssetBlock crawler type filter. |
| `AssetManager::FindFirstEntity` | `0x008755E0` | `0x004755E0` | AssetBlock crawler enumeration. |
| `AssetManager::FindNextEntity` | `0x00875610` | `0x00475610` | AssetBlock crawler enumeration. |
| `Scene::LoadMap` | `0x008932A0` | `0x004932A0` | Crawler loads registered Folder blocks. |
| `Scene::UpdateLoadedBlocks` | `0x008986E0` | `0x004986E0` | Crawler commits loaded blocks. |
| `Scene::Update` | `0x00897450` | `0x00497450` | Scene update vtable hook target. |
| `Scene::Destroy` | `0x00895E40` | `0x00495E40` | Crawler bootstrap. |
| `Scene::Start` | `0x0089A100` | `0x0049A100` | Crawler bootstrap. |
| `Scene::Reset` | `0x0089A1A0` | `0x0049A1A0` | Crawler bootstrap. |
| `KapowEngine::OpenScene` | `0x0093CE00` | `0x0053CE00` | Crawler bootstrap. |
| Texture asset allocator global | `0x00A3BE18` | `0x0063BE18` | Historical sky asset load probe. |
| Texture asset load flag global | `0x00A3BE2D` | `0x0063BE2D` | Historical sky asset load probe. |
| Camera-system global | `0x00A3DCBC` | `0x0063DCBC` | Camera slot/farclip patching. |
| Sky mesh array global | `0x00A3E0B0` | `0x0063E0B0` | Fallback SkyBox mesh detection. |

Required TOD offsets:

| Offset | Meaning |
|---:|---|
| camera system `+0x60` | primary/requested camera slot |
| camera system `+0x64` | secondary/requested camera slot |
| camera system `+0x6C` | current camera slot |
| camera `+0xB8` | near clip |
| camera `+0xBC` | far clip |
| render mesh `+0x0C` | vertex-buffer wrapper |
| render mesh `+0x10` | index-buffer wrapper |
| vertex-buffer wrapper `+0x24` | D3D vertex buffer pointer |
| index-buffer wrapper `+0x1C` | D3D index buffer pointer |

TOD function pointer types needed by the hook:

```cpp
using TodSkyBoxRenderFn = void (__fastcall*)(void* self, void* edx);
using TodRenderListSetMaterialFn =
    void (__fastcall*)(void* self, void* edx, void* material, DWORD stage);
using TodRenderListAddMeshFn = void (__fastcall*)(void* self, void* edx, void* mesh);
using TodRenderMeshDrawFn = void (__fastcall*)(void* self, void* edx, void* mesh);
using TodLoadNativeResourceFn = void* (__cdecl*)(char* resourcePath);
using TodTextureDrawAllTexturesFn = void (__cdecl*)();
using TodSceneUpdateFn = void (__fastcall*)(void* self, void* edx);
using TodFindFirstEntityFn = void* (__thiscall*)(void* assetManager);
using TodFindNextEntityFn = void* (__thiscall*)(void* assetManager, void* entity);
using TodSceneLoadMapFn = void (__thiscall*)(void* scene, unsigned int slot, void* folder);
using TodSceneUpdateLoadedBlocksFn =
    void (__thiscall*)(void* scene, int performCallbacks, void* callbackEntity);
using TodSceneDestroyFn = void (__thiscall*)(void* scene);
using TodSceneStartFn = void (__thiscall*)(void* scene);
using TodSceneResetFn = void (__thiscall*)(void* scene);
using TodKapowEngineOpenSceneFn =
    int (__thiscall*)(void* engine, const char* scenePath);
using TodSetTraverseDistanceFn = void (__thiscall*)(void* self, float distance);
```

Core runtime state to rebuild:

| State | Minimal fields |
|---|---|
| `DeclInfo` | declaration pointer, `hasPosition`, `hasPositionT` |
| `LayoutState` | current FVF, declaration, vertex shader, `fvfRhw`, `declarationPositionT`, `shaderActive`, `unknown` |
| `SurfaceInfo` | surface pointer, owner texture, width, height, format, usage, pool, `fromTexture` |
| `TextureInfo` | texture pointer, level-0 surface, width, height, levels, format, usage, pool |
| `TextureCopyInfo` | source texture, replacement texture, attempted flag |
| `TodSkyTextureRecord` | texture pointer, RTX hash, valid flag, drawn-logged flag |
| `TodForcedTextureRecord` | texture pointer, RTX hash, valid flag |
| `TodAssetCaptureConfig` | enabled, start delay, settle frames, probe frames, LOD factor list |
| `TodAssetCrawlerConfig` | enabled, include maps/missions/cutscenes/playerdata, settle frames |
| `SunLightConfig` | direction, diffuse RGB, specular scale, range |
| `TodRenderDistanceConfig` | enabled, LOD factor, far clip, minimum traverse distance |
| `TodTextureQualityConfig` | enabled, mip bias, max anisotropy, sampler count, force top mip |

Current compile-time feature flags:

```cpp
kRedirectFullSizeSceneRtToPrimary = true
kSkipRtTextureCompositesToPrimary = true
kRedirectAllColorRtTexturesToPrimary = true
kSkipAllRtTextureComposites = true
kPreloadManagedTexturesBeforeBind = false
kBindDefaultCopiesForManagedTextures = true
kCopyCompressedManagedTextures = true
kCopyWhitelistedUncompressedManagedTextures = true
kBindPreTransformedA8CopiesAfterWorld = true
kBindManagedA8DrawCopies = true
kPatchTodCameraFarClip = true
kMarkTodSkyDrawsWithViewportMinZ = true
kEnableTodSkyTextureDiscovery = true
kHookTodSkyBoxRenderTextureScope = true
kEnableTodFogStateLog = false
kEnableTodSkyAssetLoadProbe = false
kInjectSunLight = false
kEnableSunAutoAim = false
kEnableCameraResend = true
kCaptureBillboardIndices = false
kRemapBillboardIndices = false
kFreezeAnimatedAlphaWorldVertexBuffers = false
```

Core behavior requirements:

- Camera resend: cache non-identity `D3DTS_VIEW` and latest `D3DTS_PROJECTION`; before fixed-function non-pretransformed draws, resubmit view/projection through the original `SetTransform`. Skip when current view is identity, layout is RHW/`POSITIONT`, or a vertex shader is active.
- Far clip: on `Present`, read camera-system slots `+0x6C`, `+0x60`, `+0x64`; validate committed writable camera memory and sane finite near/far clip; write camera `+0xBC` to INI far clip if lower.
- Render-target support: register surfaces/textures on creation; track current RT0; treat primary as the active full-size non-texture RT; redirect selected color render-target texture surfaces to primary; skip configured pretransformed RT texture composites.
- Managed textures: register every created texture; hook texture release; for supported managed non-RT textures, create a DEFAULT-pool replacement via SYSTEMMEM staging and `UpdateTexture`; bind the DEFAULT copy so Remix gets stable upload hashes.
- Texture hashes: compute Remix-compatible XXH3 over mip 0 packed CPU upload buffer for supported formats.
- Layout classification: `FVF` RHW or declaration `POSITIONT` means pretransformed UI/overlay; non-RHW fixed-function indexed primary draws mark `worldDrawnThisFrame`.
- A8 handling: after world is drawn, selected pretransformed or managed-A8 draw categories can temporarily bind a DEFAULT copy for draw, then restore original texture0.
- Sky: inline hooks record meshes emitted while inside `SkyBox::Render`; when those meshes reach `DrawIndexedPrimitive`, temporarily set viewport `MinZ=0.999`, draw, then restore viewport. Remix uses `rtx.skyMinZThreshold = 0.99`.
- Sun: source still contains a directional-light injection/auto-aim path, but packaged flags `kInjectSunLight=false` and `kEnableSunAutoAim=false` keep it inactive. To rebuild that branch, `Present` creates a `D3DLIGHT_DIRECTIONAL` at `LightIndex=0`; auto-aim identifies the sun sprite by INI Remix hash, reads the billboard center from indexed or UP vertex data, transforms it by cached world matrix, derives eye position from view, and latches `-normalize(center - eye)` as light direction.
- Asset capture: when enabled, save `Script::LodFactor`, cycle INI LOD factors, call `Texture::DrawAllTextures()` for configured probe frames, dump unique hash-named DDS files, then restore original LOD factor.
- Asset crawler: hook `Scene::Update`; bootstrap `Overdose.scene`; enumerate Folder blocks through AssetManager; load each block with `Scene::LoadMap`; commit via `Scene::UpdateLoadedBlocks`; wait settle frames; trigger asset capture; freeze normal scene update while crawler is controlling blocks.
- Texture quality: source contains hot-reload `[TextureQuality]` support with defaults even if the INI section is absent: enabled, mip bias `-2.0`, max anisotropy `16`, sampler count `4`, force-top-mip false.
- Historical LOD/traverse support: `ApplyTodLodFactorOverride()` and `Hook_TodSetTraverseDistance()` exist, but the LOD override function is not called in the current source and the traverse hook is passthrough. Treat these as retained diagnostics, not active render-distance behavior.

Current INI schema; `[Sun]` is inactive in this package unless the ASI is rebuilt with sun injection enabled:

```ini
[RenderDistance]
Enabled=1
FarClip=8000.0

[Sun]
DirectionX=-0.48
DirectionY=-0.8
DirectionZ=-0.25
DiffuseR=3.4
DiffuseG=3.05
DiffuseB=2.55
SpecularScale=1.0
Range=100000.0
AutoAim=1
SpriteHashHex=0x5EF9EBC260F4B6BC

[AssetCapture]
Enabled=0
StartDelayFrames=180
SettleFrames=120
ProbeFrames=3
LodFactors=0.05,1.0,8.0

[AssetCrawler]
Enabled=0
IncludeMaps=1
IncludeMissions=1
IncludeCutscenes=0
IncludePlayerdata=1
LoadSettleFrames=300
```

Optional source-supported INI keys not present in the current file:

```ini
[TextureQuality]
Enabled=1
MipBias=-2.0
MaxAnisotropy=16
SamplerCount=4
ForceTopMip=0
```

## D3D9 Backend

Confirmed `TOD.exe` owns a normal D3D9 fixed-function renderer:

| Area | Output |
|---|---|
| D3D backend init | `FUN_0045e620` |
| `IDirect3D9::CreateDevice` | `FUN_00451110`, call via vtable `+0x40` |
| Adapter/global pointer | `DAT_00a39f14` |
| D3D device pointer | adapter object offset `+0x00` |
| Renderlist interpreter | `FUN_004342c0` |
| Central render-target/depth switch | `FUN_0044e220` |
| Central texture flush/bind | `FUN_0044f8a0` -> `FUN_004634b0` |

Draw API search:

- `DrawPrimitive`, vtable `+0x144`: 11 call sites.
- `DrawIndexedPrimitive`, vtable `+0x148`: 3 call sites.
- `DrawPrimitiveUP`: no call sites found.
- `DrawIndexedPrimitiveUP`: no call sites found.

## Geometry Paths

Model class:

| Symbol | Output |
|---|---|
| Model registration/property setup | `FUN_00889800` |
| Model constructor | `FUN_00884ba0` |
| Model render/renderlist emission | `FUN_00884eb0`, force-created in Ghidra |

Static/default world model path:

```text
FUN_00884eb0
  -> FUN_00432c70
  -> renderlist opcode 0x11
  -> FUN_004342c0
  -> FUN_004540e0
  -> SetStreamSource + SetFVF + SetIndices + DrawIndexedPrimitive
```

Alternate/multistream model path:

```text
FUN_00884eb0
  -> FUN_00432cc0
  -> renderlist opcode 0x12
  -> FUN_004342c0
  -> FUN_00454660
  -> SetStreamSource stream 0 + SetStreamSource stream 1
     + SetVertexDeclaration + DrawIndexedPrimitive
```

Generic 3DNUV path:

```text
FUN_00884980
  -> FUN_00432d70
  -> renderlist opcode 0x15
  -> FUN_004342c0
  -> FUN_004507b0
  -> SetFVF 0x112 + indexed draw wrapper
```

Vertex format outputs:

- Main world FVF: `0x112`.
- `0x112` = `D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1`.
- TOD labels this as `D3DFVF_3DNUV` in its own SW-skinning error string.
- XYZRHW/screen-space formats exist, but they are UI/simple primitive paths, not the default world mesh path.
- Exact searches found no `CreateVertexShader`, `SetVertexShader`, `CreatePixelShader`, or `SetPixelShader` COM calls in `TOD.exe`; the declaration path is not proven programmable-shader rendering.

Remix implication: the game is not a pure pretransformed/fullscreen-quad renderer. Real indexed 3D geometry with world/view/projection state reaches D3D9.

## Camera And Transforms

Confirmed `SetTransform` call sites:

| Address | Function | Transform |
|---|---|---|
| `0044df03` | `FUN_0044def0` | `D3DTS_WORLD = 0x100` |
| `0044e548` | `FUN_0044e400` | `D3DTS_VIEW = 2` |
| `0044e667` | `FUN_0044e580` | `D3DTS_PROJECTION = 3` |
| `0044d5c2` | `FUN_0044d2e0` | texture transform |
| `0044d89e` | `FUN_0044d5d0` | texture transform/reset |
| `0044ef43` | `FUN_0044ee70` | texture transform |

Main render-entry loop:

```text
FUN_00421530
  -> applies target/viewport state
  -> calls FUN_0044e580 to submit projection
  -> conditionally calls FUN_0044e400 to submit view
  -> flushes render command lists through FUN_004342c0
```

View conversion:

- `FUN_0044e400` copies a 4x4 camera/source matrix, builds a D3D view matrix through `FUN_004687d0`, then submits `D3DTS_VIEW`.
- `FUN_004687d0` calls `FUN_004684e0`, which performs rigid-transform inverse/transpose style conversion.

Projection:

- `FUN_0044e580` builds projection via `FUN_009676b4`.
- `FUN_009676b4` uses a standard D3D perspective form:

```text
zScale = far / (far - near)
m[10] = zScale
m[11] = 1.0
m[14] = -(zScale * near)
```

## Render Distance, Culling, LOD

Camera-system and clip outputs:

| Item | Output |
|---|---|
| Camera-system global VA | `00A3DCBC` |
| Camera-system RVA | `0063DCBC` |
| Primary/requested camera slot | `+0x60` |
| Secondary/requested camera slot | `+0x64` |
| Current camera slot | `+0x6C` |
| Near clip field | camera `+0xB8` |
| Far clip field | camera `+0xBC` |

Relevant strings/properties:

```text
farclip                string @ 009CC9D8, reference @ 0087E319
nearclip               string @ 009CCA00, reference @ 0087E2CA
set_lod_factor         string @ 009BC88C, reference @ 0048786E
forcelodcalculation    string @ 009CD758, reference @ 00890F07
lod_distance           string @ 009CE0D8, reference @ 00890359
traverse_distance      string @ 009CE100, reference @ 0089023D
lod_threshold          string @ 009CE158, reference @ 00890158
```

Default frustum constants from `FUN_004087c0 -> FUN_00406e60`:

```text
0x428C0000 = 70.0f
0x42480000 = 50.0f
0x447A0000 = 1000.0f
```

Culling:

- `FUN_004086a0` is a compact frustum/sphere visibility test.
- `FUN_004540e0` and `FUN_00454660` call it before indexed draws.
- If TOD culls a mesh before D3D submission, Remix cannot capture it.

Current shim/config:

- `TODRemixShim.cpp`: `kPatchTodCameraFarClip = true`.
- `scripts/TODRemixShim.ini`: `[RenderDistance] Enabled=1`, `FarClip=8000.0`.
- The shim no longer treats LOD factor/traverse distance as a render-distance fix. LOD probing exists only for asset capture.

## Render Targets

Confirmed central RT path:

- `FUN_0044e220` centralizes `SetRenderTarget`, depth-stencil, and viewport switching.
- TOD has fullscreen texture/render-target commands in `FUN_004342c0`.

Current active Remix RT config:

```ini
rtx.raytracedRenderTarget.enable = True
rtx.raytracedRenderTargetTextures = 0x1E604227861E0BCB, 0x261F0A146E18E6FA,
  0x4E44041F9E27548B, 0x4E44041F9E27548E, 0x572A88A0A2E43086,
  0x609466F92E702865, 0xB566CD7C690A3630, 0xD824D823D9CCF030,
  0xF885C65160789A8B
```

Current shim RT flags:

```cpp
kRedirectFullSizeSceneRtToPrimary = true
kSkipRtTextureCompositesToPrimary = true
kRedirectAllColorRtTexturesToPrimary = true
kSkipAllRtTextureComposites = true
```

RenderDoc output:

- Five analyzed captures contained only final fullscreen generated Vulkan draws over black/white D3D9 primary targets.
- No analyzed capture had indexed draw actions.
- Those captures prove the captured primary/final targets were genuinely black/white, but they are not useful evidence against TOD's real D3D9 indexed world path.

## Texture Binding And Hashes

Texture bind path:

- `FUN_0044f8a0` flushes exactly two dirty texture stages.
- Bound wrappers are near adapter offset `0x96f8`.
- Dirty flags are near adapter offset `0x9708`.
- Real D3D bind is `FUN_004634b0`.

Remix texture hash output:

- Candidate tooling computes the normal dxvk-remix texture-upload hash path: `XXH3_64bits()` over mip 0's packed CPU upload buffer for common 2D formats.
- This is the hash family used by config sets such as `rtx.uiTextures`, unless obsolete upload hashing is enabled.

Current managed-texture support:

```cpp
kBindDefaultCopiesForManagedTextures = true
kCopyCompressedManagedTextures = true
kCopyWhitelistedUncompressedManagedTextures = true
kBindPreTransformedA8CopiesAfterWorld = true
kBindManagedA8DrawCopies = true
```

Current DXVK option:

```ini
d3d9.evictManagedOnUnlock = True
```

Important boundary:

- Broad opaque managed-A8 world copying caused sky/world material corruption in earlier builds.
- Accepted handling is narrow and state-driven: HUD/UI, alpha/effects, selected albedo cases, and known managed texture hash support. Content hashes belong in Remix config, not hardcoded C++.

## UI And HUD

Confirmed behavior:

- Menu/fullscreen UI is RHW screen-space before world geometry.
- In-game HUD is RHW screen-space after world geometry.
- Remix skips pretransformed draws unless their texture hash is tagged in `rtx.uiTextures`.
- Main menu/HUD recovery is config-owned texture tagging plus managed-texture upload/hash support, not a hardcoded ASI content list.

Current `scripts/rtx.conf` owns the active `rtx.uiTextures` set. It includes positive UI/HUD hashes plus negative removals for false positives:

```text
-0x51D36BDF7C5C3E9D
-0x6F26C2C14C981112
-0x72EAE28ED2166958
-0x7ECA4C36B688596C
```

Evidence files:

- `hud_candidates_contact_sheet.png`
- `reference_hud_no_remix.jpg`
- `hud_pause_candidate_png_2100/`
- `hud_pause_latest_png/`
- `latest_ui_highres_png/`
- `ui_candidate_B3D5FD718630F8B2.png`

## Sky, Fog, Atmosphere

SkyBox engine path:

| Item | Output |
|---|---|
| SkyBox class registration | `FUN_008f22a0` |
| SkyBox constructor | `FUN_008f2360` |
| Sky mesh/resource builder | `FUN_008f20b0`, globals near `DAT_00a3e0b0` |
| Sky render | `SkyBox::Render @ 0x008F1E10` |

Runtime marker path:

```text
SkyBox::Render
  -> FUN_00431660(renderList, material, stage)
  -> FUN_00432C70(renderList, mesh)
  -> FUN_004342c0
  -> FUN_004540E0(mesh)
  -> DrawIndexedPrimitive
```

Current sky support:

```cpp
kMarkTodSkyDrawsWithViewportMinZ = true
kEnableTodSkyTextureDiscovery = true
kHookTodSkyBoxRenderTextureScope = true
```

```ini
rtx.skyAutoDetect = 0
rtx.skyMinZThreshold = 0.99
rtx.skyBrightness = 1.5
```

Sky conclusion:

- The runtime SkyBox mesh marker reaches the actual SkyBox draws.
- Static `rtx.skyBoxTextures` lists are useful evidence/fallback but are not the preferred all-mission path.
- Warm dawn/dusk appearance is not just a missing sky texture; TOD's look also depends on fog/haze, tonemapping, and lighting.

Fog outputs:

- TOD has real fixed-function fog strings/commands: `CMD_ENABLEFOG`, `CMD_SETFOGPROPERTIES`, `CMD_PUSH_FOG`, `CMD_POP_FOG`, `PCGFOG_Controller`, `Trigger_Activate_Fog`, `fog_type`.
- Fog state logger confirmed world fog such as `FOGCOLOR=0x00777952` -> RGB `(119,121,82)`, `FOGSTART=10.0`, `FOGEND=1400.0`, `RANGEFOGENABLE=1`, linear vertex fog.
- Every fog state change observed in that run had `skyScope=0`; TOD did not set fog inside `SkyBox::Render`.

Current target config has:

```ini
rtx.enableFog = False
rtx.volumetrics.enable = True
rtx.volumetrics.froxelMaxDistanceMeters = 3000
rtx.volumetrics.transmittanceColor = 0.94, 0.92, 0.88
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 180
```

## Lighting And Shadows

Current target runtime files as of 2026-06-08:

- `TODRemixShim.cpp`: `kInjectSunLight = false`.
- `TODRemixShim.cpp`: `kEnableSunAutoAim = false`.
- `scripts/TODRemixShim.ini`: `[Sun]` is an inactive reference block: direction `(-0.48, -0.8, -0.25)`, diffuse `(3.4, 3.05, 2.55)`, `SpecularScale=1.0`, `Range=100000.0`, `AutoAim=1`, `SpriteHashHex=0x5EF9EBC260F4B6BC`.
- `scripts/rtx.conf`: `rtx.ignoreGameDirectionalLights = False`.
- `scripts/rtx.conf`: Remix local tonemapper is active with `rtx.localtonemap.shadows = 1.4`.

Sun auto-aim output:

- The shim can identify TOD's lens-flare/sun sprite by config hash, read its billboard center from VB/UP geometry, transform it by the cached world matrix, derive camera position from the view matrix, and aim a D3D directional light opposite the visible sun.
- This package is the skybox-shadow/local-tonemap branch: injected sun code remains in source for rebuilds, but it is compiled off to avoid double or opposite shadows in the packaged default.

## Moving Geometry / Instance Matching

Root output:

- The moving texture/geometry displacement is Remix previous-frame instance matching, not TOD texture hashes, sky, fog, DLFG, render-target routing, or world transform resend.
- `rtx.enableInstanceDebuggingTools = True` removed displacement but caused broad temporal blur, so it is diagnostic-only.
- `rtx.uniqueObjectDistance = 25.0` is the best saved native Remix compromise.

Current config:

```ini
rtx.captureInstances = True
rtx.uniqueObjectDistance = 25.0
```

Observed rejected values:

- Default `300.0`: too permissive for TOD, causes wrong previous-frame instance reuse.
- `75.0`: still too permissive.
- `15.0`: less displacement but visible player/car blur.
- `20.0`: compromise tested; user restored `25.0`.

## Per-Map Asset Capture

Confirmed addresses:

```text
Script::LodFactor          0x00A12090
Texture::DrawAllTextures() 0x00463850
Texture::TexturesMap       0x00A39F50
```

Static manifest:

- File: `tod_asset_archive_manifest.json`.
- 756 relevant block/texture entries.
- Shared maps found: `map01`, `map02`, `map03`, `map04`, `map06`, `map07`, `map08`, `map09`, `map10`.
- No shared `map05` block in the archive.
- `AssetBlockReader` builds but is incomplete for shipped submaps and crashes on real map submaps; do not treat `dump-blocks` as reliable.

Runtime capture:

- Config: `scripts/TODRemixShim.ini`.
- `[AssetCapture] Enabled=0` by default.
- When enabled, the shim saves current `Script::LodFactor`, cycles configured LOD factors, calls TOD's `Texture::DrawAllTextures()`, computes the Remix XXH3 hash, dumps unique DDS files, then restores the LOD factor.
- Output: `rtx-remix/logs/tod-forced-texture-probe`.
- Status: `rtx-remix/logs/tod-asset-capture.log`.

Crawler:

- `[AssetCrawler] Enabled=0` by default.
- Uses TOD's own `Scene::LoadMap` / `Scene::UpdateLoadedBlocks` path.
- Completed catalog: 114 live entries across maps, submaps, missions, arcade/intermission blocks, and playerdata. Cutscenes disabled in the verified run.
- Completed run ended with `completed completed=114 total=114`.

Verified capture/upscale output from the notes:

```text
Whole-game crawl unique textures: 3390
Capture union:                    3554
I2M-complete texture sets:         2681
Too small for I2M:                  872
Diffuse DDS:                       2681
Normal DDS:                        2681
Roughness DDS:                     2681
Alpha sources preserved:           525 / 525
I2M failed hashes:                   0
USDA materials:                    2681
```

Remaining limitation: the crawler covers registered live AssetBlocks through proper scene/resource-controller paths. It cannot invent mission-script-only procedural variants, gameplay-event spawns, or cutscene/video blocks while cutscenes are disabled.

## Evidence Map

Full source notes:

- [archive/source_notes/rtx_remix_re_findings.md](archive/source_notes/rtx_remix_re_findings.md)
- [archive/source_notes/rtx_remix_re_pass2_model_path.md](archive/source_notes/rtx_remix_re_pass2_model_path.md)
- [archive/source_notes/rtx_remix_ghidra_support_design.md](archive/source_notes/rtx_remix_ghidra_support_design.md)
- [archive/source_notes/tod_render_distance_texture_handling_re_20260603.md](archive/source_notes/tod_render_distance_texture_handling_re_20260603.md)
- [archive/source_notes/rtx_remix_runtime_debug_log.md](archive/source_notes/rtx_remix_runtime_debug_log.md)
- [archive/source_notes/tod_per_map_asset_capture_20260607.md](archive/source_notes/tod_per_map_asset_capture_20260607.md)
- [archive/source_notes/rtx_remix_candidate_report.md](archive/source_notes/rtx_remix_candidate_report.md)
- [archive/source_notes/renderdoc_black_screen_analysis.md](archive/source_notes/renderdoc_black_screen_analysis.md)
- [archive/source_notes/state7_STATE.md](archive/source_notes/state7_STATE.md)
- [archive/source_notes/runtime_package_layout.md](archive/source_notes/runtime_package_layout.md)
- [archive/source_notes/GHIDRA_VARIABLE_APIS_EXPLAINED.md](archive/source_notes/GHIDRA_VARIABLE_APIS_EXPLAINED.md)

Markdown source evidence kept in `archive/source_notes/`:

- `rtx_remix_re_findings.md`
- `rtx_remix_re_pass2_model_path.md`
- `rtx_remix_candidate_report.md`
- `renderdoc_black_screen_analysis.md`
- `runtime_package_layout.md`
- `rtx_remix_runtime_debug_log.md`
- `rtx_remix_ghidra_support_design.md`
- `GHIDRA_VARIABLE_APIS_EXPLAINED.md`
- `state7_STATE.md`
- `tod_per_map_asset_capture_20260607.md`
- `tod_render_distance_texture_handling_re_20260603.md`

This package does not ship the large raw capture/contact-sheet folders in `re_docs/`; keep those in the development workspace or backups when needed.
