# Total Overdose: RTX Remix

This is a **reverse-engineering project to make RTX Remix run well on the 2004 game
*Total Overdose: A Gunslinger's Tale in Mexico***. The game is fixed-function
**Direct3D 9** with baked vertex lighting, so a lot of work
went into getting Remix to path-trace it correctly: valid texture hashes, sky/HUD
handling, fog/atmosphere, render distance, and lighting. The `re_docs/` folder documents
that whole process in detail, and `tools/` holds the source of the custom compatibility
shim.

It is a work in progress, not an official release.

## Requirements

- **The GOG release of Total Overdose**:
  <https://www.gog.com/en/game/total_overdose_a_gunslingers_tale_in_mexico>.
  This matters because the compatibility shim's TOD-specific hooks (e.g. the skybox-render
  marker used for sky handling) target function addresses found in the **GOG `TOD.exe`
  build** via Ghidra. Other releases (retail/Steam/cracked) have different binaries and are
  not supported; sky/lighting handling will misbehave.
- Windows with an RTX Remix-capable NVIDIA GPU + driver.

## What's in here

| Path | What it is |
|------|------------|
| `.trex/` | The bundled RTX Remix runtime (path tracer). |
| `scripts/TODRemixShim.asi` | The compatibility shim (D3D9 vtable hook): camera resend, render-target redirect, managed-texture shadowing for valid Remix hashes, sky-draw marking. |
| `scripts/TODRemixShim.ini` | Runtime-tunable settings for the shim. |
| `scripts/rtx.conf` | The RTX Remix config for this game (the active one; Remix reads `scripts/`). |
| `scripts/dxvk.conf` | DXVK option layer (parsed separately from `rtx.conf`). |
| `scripts/TotalOverdose.WidescreenFix.asi/.ini` | Widescreen fix plugin. |
| `scripts/rtx-remix/mods/mission2/` | Mission 2 AI-upscaled PBR texture mod (diffuse/normal/roughness). |
| `d3d9.dll`, `dinput8.dll`, `NvRemixLauncher32.exe` | RTX Remix bridge (D3D9), ASI loader, launcher. |
| `d3d8to9.dll` | Legacy D3D8-to-9 wrapper from the Remix install template; unused by TOD (which is D3D9). Safe to delete. |
| `tools/tod_remix_shim/` | **Source** of the shim (`TODRemixShim.cpp`) + `build.bat`. |
| `re_docs/` | Trimmed reverse-engineering reference: factual hook outputs plus a brief work log. Original notes are archived under `re_docs/archive/source_notes/`. |


## Lighting Configurations

The packaged default is the skybox-shadow/local-tonemap branch:

```ini
# tools/tod_remix_shim/TODRemixShim.cpp
kInjectSunLight = false
kEnableSunAutoAim = false

# scripts/rtx.conf
rtx.skyBrightness = 1.5
rtx.localtonemap.shadows = 1.4
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 180
```

This avoids the double/opposite-shadow failure seen when Remix used both the TOD skybox/environment and an injected directional sun. The `[Sun]` block in `scripts/TODRemixShim.ini` is kept as inactive reference data for rebuilds.

Alternative injected-sun/bright-sky configuration tested in `C:\GOG Games\Total Overdose`:

```ini
# tools/tod_remix_shim/TODRemixShim.cpp
kInjectSunLight = true
kEnableSunAutoAim = true

# scripts/TODRemixShim.ini
DirectionX = -0.48
DirectionY = -0.8
DirectionZ = -0.25
DiffuseR = 6.0
DiffuseG = 5.4
DiffuseB = 4.5
SpecularScale = 1.0
Range = 100000.0
AutoAim = 1
SpriteHashHex = 0x5EF9EBC260F4B6BC

# scripts/rtx.conf
rtx.tonemappingMode = 0
rtx.tonemap.dynamicRange = 10
rtx.skyBrightness = 3.5
rtx.volumetrics.transmittanceMeasurementDistanceMeters = 20
rtx.localtonemap.shadows = 0.3  # inactive while global tonemapping mode 0 is used
```

That alternative gives a brighter sky and stronger directional sun contrast, but requires rebuilding the ASI with the two source flags enabled and then retuning to avoid duplicate shadow direction.

`rtx.uniqueObjectDistance = 25.0` is intentionally kept in this package. It is the best tested balance between moving texture/geometry glitches from excessive instance matching and the player/car blurriness that appears when the value is too low. Values around `300` or `75` caused more texture glitching; values around `15` reduced those glitches but made moving actors/vehicles blurrier.

## Install
Copy the contents of this package into your GOG Total Overdose install so the files land
next to `TOD.exe` (merging the `.trex/` and `scripts/` folders). The RTX Remix runtime is
bundled, so nothing else is needed. Launch via `NvRemixLauncher32.exe` (or the GOG
launcher if it's wired to it).

## Details

For details on the current hook implementation, see **`tools/tod_remix_shim/`**. For enough RE detail to rebuild the hook, start at **`re_docs/README.md`**: `01_game_reverse_engineering_outputs.md` is the factual spec and `02_work_log_what_worked.md` is the brief decision log.

