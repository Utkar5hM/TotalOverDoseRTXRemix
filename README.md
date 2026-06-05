# Total Overdose RTX Remix Runtime Package

This is the use-only RTX Remix package for Total Overdose. It contains the files needed to run the current Remix config, ASI fixes, and Mission 2 full-PBR upscaled texture replacements.

## Requirements

- A legal installed copy of Total Overdose. This package does not include game executables, archives, audio, videos, or other game data.
- Windows with an RTX Remix-capable GPU/driver setup.

## Layout

- `d3d8to9.dll`, `d3d9.dll`, `NvRemixLauncher32.exe` - RTX Remix/D3D8 runtime pieces.
- `dinput8.dll` - ASI loader used by the script plugins.
- `scripts/rtx.conf` - the single RTX Remix config for this package.
- `scripts/dxvk.conf` - DXVK option layer; kept separate because Remix/DXVK parses it as a different config file.
- `scripts/` - script-side camera resend ASI and widescreen ASI.
- `scripts/rtx-remix/mods/mission2/` - the active RTX Remix Mission 2 mod.
- `scripts/rtx-remix/mods/mission2/mod.usda` - Remix mod entry layer.
- `scripts/rtx-remix/mods/mission2/ai_textures.usda` - material-to-texture replacement layer.
- `scripts/rtx-remix/mods/mission2/assets/` - DDS texture replacements referenced by `ai_textures.usda`.

Not included: Toolkit captures, logs, source PNGs, authoring `deps` links, reverse-engineering notes, texture-generation scripts, duplicate root config files, or base game files.

## Texture State

Mission 2 has full generated PBR replacement wiring:

- Diffuse, normal, and roughness replacement DDS files are included.
- Diffuse replacements preserve original alpha where the original captured texture had alpha, avoiding broken foliage and cutout materials.
- The main character shirt material is excluded because the upscale looked worse in-game.
Included referenced DDS files: 2802.
Texture asset size: 0.55 GB.

## Install

Copy this package over a Total Overdose install so the files land next to `TOD.exe`.

Expected live paths after install:

- `TOD.exe`
- `d3d8to9.dll`
- `d3d9.dll`
- `dinput8.dll`
- `scripts/rtx.conf`
- `scripts/dxvk.conf`
- `scripts/TODCameraResend.asi`
- `scripts/TotalOverdose.WidescreenFix.asi`
- `scripts/rtx-remix/mods/mission2/mod.usda`
- `scripts/rtx-remix/mods/mission2/ai_textures.usda`
- `scripts/rtx-remix/mods/mission2/assets/textures/<hash>/<hash>_diffuse.dds`
- `scripts/rtx-remix/mods/mission2/assets/textures/<hash>/<hash>_normal_dx.dds`
- `scripts/rtx-remix/mods/mission2/assets/textures/<hash>/<hash>_roughness.dds`

## Notes

The old local authoring path `C:\GOG Games\todremix\mission2` is not required for runtime use. The mod is packaged directly in `scripts/rtx-remix/mods/mission2` so no symlink is needed.
