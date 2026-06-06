# Runtime Package Layout

Current runtime packaging keeps one RTX Remix config file:

- `scripts/rtx.conf` contains all RTX options, including replacement assets and user overrides.
- `scripts/dxvk.conf` remains separate because DXVK parses it as its own option layer.
- Root-level `rtx.conf`, `dxvk.conf`, and `user.conf` are intentionally not used by the package.
- `scripts/user.conf` is intentionally removed; its required values are merged into `scripts/rtx.conf`.

The runtime mod is packaged directly under:

`scripts/rtx-remix/mods/mission2/`

That folder should contain only the runtime mod payload:

- `mod.usda`
- `ai_textures.usda`
- `lighting.usda`
- `assets/textures/<hash>/*.dds` referenced by `ai_textures.usda`

The local authoring path `C:\GOG Games\todremix\mission2` is still useful while generating or editing assets, but it is not required for a use-only install.

Lighting is intentionally split into `lighting.usda`, but the current shadow-casting sun is not authored there. It is injected at runtime by `scripts/TODCameraResend.asi` as an enabled D3D9 directional light, which Remix converts to a distant light. `scripts/rtx.conf` must not set `rtx.ignoreGameDirectionalLights = True`, because that also suppresses the injected ASI sun and removes shadows.

The ASI sun is now runtime-tunable from `scripts/TODCameraResend.ini`. Current test values flip the old Z direction (`DirectionZ=-0.6`) to move shadows away from the visible Mission 2 sky sun and raise direct sun contrast while `scripts/rtx.conf` lowers broad fill (`rtx.skyBrightness=3`, `rtx.vertexColorStrength=1.15`) for darker shadows. If the angle is still off, tune `DirectionX` and `DirectionZ` there before doing deeper RE of TOD's actual sun/lens-flare vector.
