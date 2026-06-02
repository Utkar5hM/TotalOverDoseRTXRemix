# RenderDoc Black Screen Analysis

Capture directory:

`C:\Users\utkar\AppData\Local\Temp\RenderDoc`

RenderDoc:

`C:\Program Files\RenderDoc\renderdoccmd.exe`, version `1.44`

## Short Verdict

The black captures are genuinely black in the captured render targets. This is not an alpha/display issue in RenderDoc.

For four of the five `.rdc` files, both the final swapchain image and the intermediate `D3D9 texture primary` target are solid `RGB=0, A=255`. The latest capture, `frame4575`, is the same structure but solid white instead of black.

The final present path is a fullscreen generated draw:

- `vkCmdDraw()`
- 3 vertices
- no vertex buffers
- no vertex inputs
- no indexed draw calls
- final target usage: `Discard` at render pass begin, `ColorTarget` at the fullscreen draw, then barrier/present

That means the captures only contain a post-process/present-style Vulkan output path. They do not contain reconstructable D3D9 world geometry.

## Capture Summary

| Capture | Draw actions | Final draw | Final output | Primary target | Pixel shader resources reported by pipeline |
|---|---:|---|---|---|---:|
| `frame2334` | 48 | `vkCmdDraw`, 3 verts | solid black `RGBA=(0,0,0,255)` | solid black `RGBA=(0,0,0,255)` | 0 |
| `frame2381` | 48 | `vkCmdDraw`, 3 verts | solid black `RGBA=(0,0,0,255)` | solid black `RGBA=(0,0,0,255)` | 0 |
| `frame2600` | 43 | `vkCmdDraw`, 3 verts | solid black `RGBA=(0,0,0,255)` | black RGB, alpha varied | 0 |
| `frame3899` | 43 | `vkCmdDraw`, 3 verts | solid black `RGBA=(0,0,0,255)` | solid black `RGBA=(0,0,0,255)` | 0 |
| `frame4575` | 43 | `vkCmdDraw`, 3 verts | solid white `RGBA=(255,255,255,255)` | solid white `RGBA=(255,255,255,255)` | 0 |

No analyzed capture had `ActionFlags.Indexed` on any draw action.

## Evidence From `frame2334`

Final output:

```text
last_draw event=259 action=49 name='vkCmdDraw()'
flags=ActionFlags.Instanced|Drawcall
numIndices=3 numInstances=1
outputs=['Swapchain Image 1290', ...]

last_output_desc 1600x1200 fmt=R8G8B8A8_UNORM
last_output_minmax min=[0.0, 0.0, 0.0, 1.0] max=[0.0, 0.0, 0.0, 1.0]
last_output_u8_sampled=213334 min=[0, 0, 0, 255] max=[0, 0, 0, 255]
nonzero_rgb=0 zero_alpha=0 full_alpha=213334
```

Intermediate `D3D9 texture primary` before presentation:

```text
primary_target rid=ResourceId::880 event=243 1600x1200 fmt=B8G8R8A8_UNORM
primary_target_minmax min=[0.0, 0.0, 0.0, 1.0] max=[0.0, 0.0, 0.0, 1.0]
primary_target_u8_sampled=213334 min=[0, 0, 0, 255] max=[0, 0, 0, 255]
nonzero_rgb=0 zero_alpha=0 full_alpha=213334
primary_target_usage event=259 usage=ResourceUsage.PS_Resource action='vkCmdDraw()'
```

Final draw pipeline:

```text
pipeline_event=259 name='vkCmdDraw()'
pipeline_topology=Topology.TriangleStrip
pipeline_vbuffer_count=0
pipeline_vertex_input_count=0
VS_shader id=ResourceId::1348 entry='main'
FS_shader id=ResourceId::1349 entry='main'
FS_readonly_nonnull=0
PS_readonly_nonnull=0
```

## Evidence From `frame4575`

This capture is not black. It is the same final draw shape, but the target bytes are all white:

```text
last_draw event=246 action=44 name='vkCmdDraw()'
flags=ActionFlags.Instanced|Drawcall
numIndices=3 numInstances=1

last_output_desc 1600x1200 fmt=R8G8B8A8_UNORM
last_output_minmax min=[1.0, 1.0, 1.0, 1.0] max=[1.0, 1.0, 1.0, 1.0]
last_output_u8_sampled=213334 min=[255, 255, 255, 255] max=[255, 255, 255, 255]
```

The intermediate `D3D9 texture primary` is also solid white:

```text
primary_target rid=ResourceId::886 event=230 1600x1200 fmt=B8G8R8A8_UNORM
primary_target_u8_sampled=213334 min=[255, 255, 255, 255] max=[255, 255, 255, 255]
```

## Interpretation

The black screen is upstream of the final swapchain present. The final pass is copying/drawing whatever is already in `D3D9 texture primary`; in the black captures that source is already black.

The captures do not show a normal D3D9 world-geometry stream. They show DXVK/Vulkan replay of generated fullscreen draw calls and small post-process/UI-style draws. So these `.rdc` files are not useful for checking the engine's real D3D9 indexed geometry path.

Given the earlier Ghidra findings that `TOD.exe` can submit real indexed 3D geometry in D3D9, the mismatch likely comes from the capture/remix translation path or from capturing a stage where the primary render target is blank, not from the final RenderDoc output viewer.

## Generated Files

- Raw analysis logs: `C:\GOG Games\Total Overdose\renderdoc_analysis_TOD_*.txt`
- Exported final/primary PNGs: `C:\GOG Games\Total Overdose\renderdoc_exports`
- RenderDoc thumbnails: `C:\GOG Games\Total Overdose\renderdoc_thumbs`

The temporary qrenderdoc autoload config was restored to its previous empty `AlwaysLoad_Extensions` state after the analysis.
