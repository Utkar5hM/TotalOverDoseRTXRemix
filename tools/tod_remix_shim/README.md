# TODRemixShim

The RTX Remix compatibility shim for Total Overdose (Direct3D 9, fixed-function). It is a
single-file D3D9 vtable hook (`TODRemixShim.cpp`) that loads as an ASI plugin.

What it does:

- Camera resend: re-submits the latest non-identity view + projection before
  fixed-function draws so Remix detects a valid camera.
- Render-target redirect / composite skip.
- Managed-texture shadowing (SYSTEMMEM to DEFAULT) so Remix gets valid texture hashes.
- Sky-draw marking for sky handling.

The injected directional "sun" light and its auto-aim are present but disabled
(`kInjectSunLight = false`); sun shadows come from Remix's sky/environment probe. See
`re_docs/` for the full reverse-engineering history.

## Build

```bat
build.bat
```

Builds with MSVC (`/W4` clean). The game and the Remix bridge must be closed first, since
the output `.asi` is locked while loaded. It includes an xxHash header from a dxvk-remix
checkout (`../../third_party/...`); point that at a local dxvk-remix clone if you rebuild.

Output:

```text
scripts\TODRemixShim.asi
```

Runtime config (hot-reloaded): `scripts\TODRemixShim.ini`
Runtime log: `rtx-remix\logs\tod-remix-shim.log`
