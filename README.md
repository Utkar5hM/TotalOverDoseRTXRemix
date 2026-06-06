# Total Overdose — RTX Remix

This is a **reverse-engineering project to make RTX Remix run well on the 2004 Direct3D
game *Total Overdose: A Gunslinger's Tale in Mexico***. The game is fixed-function D3D8
(wrapped to D3D9) with baked vertex lighting, so a lot of work went into getting Remix to
path-trace it correctly — valid texture hashes, sky/HUD handling, fog/atmosphere, render
distance, and lighting. The `re_docs/` folder documents that process in detail, and
`tools/` holds the source of the custom compatibility shim.

It is a work in progress, not an official release.

## Requirements

- **The GOG release of Total Overdose** —
  <https://www.gog.com/en/game/total_overdose_a_gunslingers_tale_in_mexico>.
  This matters: the compatibility shim's TOD-specific hooks (e.g. the skybox-render marker
  used for sky handling) target function addresses found in the **GOG `TOD.exe` build** via
  Ghidra. Other releases (retail/Steam/cracked) have different binaries and are not
  supported — sky/lighting handling will misbehave.
- A legal copy of the game. **No game files** (executables, `*.naz` archives, audio,
  video) are included or redistributed here.
- Windows with an RTX Remix-capable NVIDIA GPU + driver.

## What's in here

| Path | What it is |
|------|------------|
| `.trex/` | The bundled RTX Remix runtime (path tracer). |
| `scripts/TODCameraResend.asi` | The compatibility shim (D3D9 vtable hook). Camera resend, render-target redirect, managed-texture shadowing for valid Remix hashes, sky-draw marking. |
| `scripts/TODCameraResend.ini` | Runtime-tunable settings for the shim. Currently the `[Sun]` section is inactive (see Lighting). |
| `scripts/rtx.conf` | The RTX Remix config for this game (the active one — Remix reads `scripts/`). |
| `scripts/dxvk.conf` | DXVK option layer (parsed separately from `rtx.conf`). |
| `scripts/TotalOverdose.WidescreenFix.asi/.ini` | Widescreen fix plugin. |
| `scripts/rtx-remix/mods/mission2/` | Mission 2 AI-upscaled PBR texture mod (diffuse/normal/roughness). |
| `d3d9.dll`, `d3d8to9.dll`, `dinput8.dll`, `NvRemixLauncher32.exe` | Bridge, D3D8→9 wrapper, ASI loader, launcher. |
| `tools/tod_camera_resend_asi/` | **Source** of the shim (`TODCameraResend.cpp`) + `build.bat`. |
| `re_docs/` | Reverse-engineering notes and the chronological runtime debug log. |

## Install

Copy the contents of this package into your GOG Total Overdose install so the files land
next to `TOD.exe` (merging the `.trex/` and `scripts/` folders). The RTX Remix runtime is
bundled, so nothing else is needed. Launch via `NvRemixLauncher32.exe` (or the GOG
launcher if it's wired to it).

## Lighting / shadows

TOD has **no real sun** — lighting is baked into vertex colors and the on-screen sun is a
skybox-painted glow plus a 2D lens-flare billboard. Sun **shadows are cast by the sky /
skybox through Remix's environment probe**, which works correctly on its own.

An earlier approach had the shim inject a D3D9 directional "sun" light for Remix to
ray-trace. Even when auto-aimed at TOD's lens-flare sun, it produced a **second, conflicting
shadow** — TOD's lens-flare sun and the skybox sun sit in different directions — so it is
**disabled** (`kInjectSunLight = false`). The injection + auto-aim machinery remains in the
source (gated off) for anyone who wants to revisit a crisp directional sun later (which
would also need `rtx.skyBrightness` lowered so the two shadows don't double up).

Shadow depth is tuned with `rtx.localtonemap.shadows` (the local tonemapper is active;
lower = darker shadows) — adjustable live in the Remix menu (Alt+X).

## Building the shim (optional)

`tools/tod_camera_resend_asi/build.bat` builds `TODCameraResend.asi` with MSVC (`/W4`
clean). It includes an xxHash header from a dxvk-remix checkout (`../../third_party/...`);
point that include at a local dxvk-remix clone if you rebuild. The prebuilt `.asi` in
`scripts/` is ready to use, so building is only needed to modify the shim.
