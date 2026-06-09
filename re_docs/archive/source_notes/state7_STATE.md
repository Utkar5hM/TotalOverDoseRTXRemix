# State 7 - RTX/HUD/Sky Baseline With Tuned Instance Matching

Saved: 2026-06-05 19:05

## Summary

- RTX rendering, HUD/menu tagging, sky handling, sun injection, and volumetric haze are preserved from the current working branch.
- Moving texture/geometry displacement was traced to Remix previous-frame instance matching, not texture hashes or TOD sky/fog handling.
- `rtx.enableInstanceDebuggingTools = True` removed the displacement but caused broad temporal blur, so it was rejected as a final setting.
- `rtx.uniqueObjectDistance = 25.0` is the saved compromise:
  tighter than Remix's default `300.0`, less blurry than `15.0`, and better than `75.0`/default for wrong previous-frame instance reuse.

## User-Visible State

- HUD is visible.
- Sky is accepted in the current form.
- Minor flicker may remain, but `25.0` is the best confirmed value so far.
- `15.0` caused noticeable blur on player/car motion, especially while driving.

## Active Key Config

```ini
rtx.uniqueObjectDistance = 25.0
rtx.captureInstances = True
rtx.raytracedRenderTarget.enable = True
rtx.dlfg.enable = True
rtx.dlfg.maxInterpolatedFrames = 1
rtx.postfx.enable = False
rtx.autoExposure.enabled = False
rtx.vertexColorStrength = 1.3
d3d9.evictManagedOnUnlock = True
```

Not active:

```ini
rtx.enableInstanceDebuggingTools
rtx.enableAlwaysCalculateAABB
rtx.useBuffersDirectly
d3d9.allowDiscard
rtx.enableNearPlaneOverride
```

## Active ASI

- `scripts/TODCameraResend.asi`
- Size: 139776 bytes
- SHA256: `80EA0C88C16DABB1911C87034BC6D69DC37891F11626FE6C53B5077B5FD50AA3`
- Source: `tools/tod_camera_resend_asi/TODCameraResend.cpp`
- Source SHA256: `60B7B9D98D144C6DCB77598BB3CA0BB6330432F0212928EBD09795C5963B6013`

## Config SHA256

- `root/rtx.conf`: `372C5863FF0E72DA6C921360FC299A9829518174CC179FB87C8E7FFA6223FCFE`
- `scripts/rtx.conf`: `8927BCED897994EF0BEA262DA288AECF097540C24C18BCEDA352C7BC8B3910E0`
- `root/dxvk.conf`: `6544699D0C289971B8E9E76FF1ADF677EC19B6619689C4ED871D17026FE7857C`
- `scripts/dxvk.conf`: `6544699D0C289971B8E9E76FF1ADF677EC19B6619689C4ED871D17026FE7857C`
- `root/user.conf`: `7D19DB06905550895D368E41F429646E60CE22BB87A14A6CE59694FE2F8F0352`
- `scripts/user.conf`: `24238055CF8821F0E58476CD8E8FD59F713E424CCB383B0DB77F9E48E91ADF51`

## Included Snapshot Layout

```text
state7/
  STATE.md
  root/       root config files
  scripts/    active ASI, script configs, widescreen files
  tools/      ASI source and build files
  re_docs/    Markdown RE/debug docs only
```

## Main Evidence

- `re_docs/rtx_remix_runtime_debug_log.md`
- `re_docs/tod_render_distance_texture_handling_re_20260603.md`
