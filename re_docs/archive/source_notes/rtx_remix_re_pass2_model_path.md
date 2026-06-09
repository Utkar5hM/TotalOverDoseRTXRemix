# Total Overdose RTX Remix RE - Pass 2

Program loaded in Ghidra: `TOD.exe`

Scope: close the `00884eb0` Model vtable gap and determine whether static world geometry mainly uses the FVF path or the vertex-declaration/shader path.

## Executive Verdict

- `00884eb0` is confirmed as the Model render/renderlist-emission method. I force-created it in Ghidra as `FUN_00884eb0`; this was the only Ghidra project write performed in this pass.
- `FUN_00884eb0` does not call D3D directly. It walks Model mesh/submesh records and emits renderlist draw opcodes. Its normal/default mesh emission is opcode `0x11`, which the renderlist flush maps to `FUN_004540e0`, the indexed FVF draw path.
- Static/non-special model geometry predominantly appears to flow through opcode `0x11` -> `FUN_004540e0` -> `SetStreamSource` + `SetFVF` + `SetIndices` + `DrawIndexedPrimitive`. Confidence: high from the default branch and xref distribution. Runtime frequency would require instrumentation, but the static code strongly favors this path.
- Opcode `0x12` exists and flushes to `FUN_00454660`, a multistream vertex-declaration draw path. In the Model renderer it is reached only through a gated alternate-resource path (`FUN_0085e690(...) != 0`), not as the default draw.
- Exact instruction searches found no D3D9 COM calls at `CreateVertexShader` (`0x16c`), `SetVertexShader` (`0x170`), `CreatePixelShader` (`0x1a8`), or `SetPixelShader` (`0x1ac`). The declaration path uses `SetVertexDeclaration` (`0x15c`) but I found no paired programmable shader calls.

## Goal A - `00884eb0`

Action taken:

```text
create_function 00884eb0, disassemble_first=true
result: function created as FUN_00884eb0, body size 9643 bytes
```

Xrefs:

```text
00884eb0:
  009cce68 DATA               ; Model vtable slot +0x20
  008cdb19 UNCONDITIONAL_CALL
  008d1809 UNCONDITIONAL_CALL
```

The decompiled function is a large Model renderer. It starts from Model instance state (`param_1`), checks LOD/opacity, walks mesh groups from `param_1 + 0x50`, selects buckets, sets matrix/material/texture state, then emits draw records.

Key decompiled evidence from `FUN_00884eb0`:

```c
void __fastcall FUN_00884eb0(int param_1)
{
  bVar16 = *(byte *)(*(int *)(param_1 + 0x30) + 0x51);
  psVar15 = (short *)(uint)bVar16;
  if ((short *)0x5 < psVar15) {
    *(byte *)(param_1 + 0x6b) = bVar16;
    *(byte *)(param_1 + 0x14) = *(byte *)(param_1 + 0x14) | 8;
    return;
  }

  local_154 = (uint)((float)(*(uint *)(param_1 + 0x6c) & 0xff) *
                     (float)*(byte *)(*(int *)(param_1 + 0x30) + 0x52) *
                     DAT_009b38e8);
  if (local_154 == 0) {
    *(undefined1 *)(param_1 + 0x6a) = 0;
    *(byte *)(param_1 + 0x6b) = bVar16;
    *(byte *)(param_1 + 0x14) = *(byte *)(param_1 + 0x14) | 8;
    return;
  }
```

Mesh/submesh iteration:

```c
local_6c = *(uint **)(*(int *)(param_1 + 0x50) + 0x34);
local_1f8 = 0;
if (0 < (int)local_6c) {
  do {
    iVar17 = local_1f8 * 0x7c;
    if (*(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x28 + iVar17) != 0) {
      ...
      local_1f4 = (int *)0x0;
      if (0 < *(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x28 + iVar17)) {
        local_148 = 0.0;
        do {
          iVar13 = *(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x24 + iVar17);
          puVar12 = (uint *)(iVar13 + (int)local_148);
          local_138 = puVar12;
          if ((short *)(*(uint *)(iVar13 + 0x38 + (int)local_148) & 0x1f) == local_158) {
            uVar9 = *puVar12;
            local_13c = uVar9 & 0xfffffffc;
            local_68 = FUN_0088a1c0(puVar12,local_150);
            local_1ac = (int *)0x0;
            if ((*(uint *)((uVar9 & 0xfffffffc) + 4) >> 2 & 1) != 0) {
              local_1ac = (int *)FUN_0088a200(puVar12,local_150,&local_70);
            }
```

Bucket selection: non-extra-stream meshes use buckets `0..3`; extra-stream/declaration candidates use buckets `4..5`.

```c
if (local_1ac == (int *)0x0) {
  if (local_140 == 1) {
    uVar9 = 1;
  }
  else if (local_140 < 2) {
    uVar9 = -(uint)bVar16 & 3;
  }
  else {
    uVar9 = 2;
  }
}
else {
  uVar9 = bVar16 + 4;
}

iVar13 = *(int *)(param_1 + 0x7c + uVar9 * 4);
...
if (iVar13 == 0) {
  iVar13 = FUN_004053c0(0x5c,0,0);
  if (iVar13 != 0) {
    iVar13 = FUN_00436b00((int)*(short *)(param_1 + 0x68),0x46,0x1d);
  }
  *(int *)(param_1 + 0x7c + uVar9 * 4) = iVar13;
}
else {
  FUN_00436ae0();
}
```

The draw emission point:

```c
FUN_00431660(local_68,0);
if (local_1ac != (int *)0x0) {
  if (0 < (int)local_140) {
    FUN_00433650(local_140);
  }
  FUN_00431660(local_1ac,1);
  FUN_00433910(local_70);
  FUN_00433890(1);
}

...
iVar13 = 0;
if (bVar4) {
  iVar13 = FUN_0085e690(*(undefined4 *)
             (*(int *)(param_1 + 0xb0) +
             ((uint)*(byte *)(*(int *)(param_1 + 0xa0) + 4 + local_1f8 * 8) +
              (int)local_1f4) * 4));
}

if (local_14c == 0.0) {
  iVar14 = FUN_00431540(&local_1a8);
}
if (iVar13 == 0) {
  FUN_00432c70(local_13c);
}
else {
  FUN_00432cc0(iVar13);
}
```

Conclusion for Goal A: confirmed. `00884eb0` is the Model render method that feeds Model mesh buffers into the renderlist. It emits opcode `0x11` normally, and opcode `0x12` for a gated alternate-resource/multistream case. It is not itself the D3D draw routine; the Adapter draw happens later when `FUN_004342c0` flushes the renderlist.

## Build-Side Opcodes

`FUN_00432c70` writes opcode `0x11` plus one payload pointer:

```c
void __thiscall FUN_00432c70(int param_1, undefined4 param_2)
{
  iVar1 = *(int *)(param_1 + 0x20) + 1;
  if (*(int *)(param_1 + 0x18) < iVar1) {
    FUN_00415510(iVar1);
  }
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = 0x11;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
  ...
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = param_2;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
}
```

`FUN_00432cc0` writes opcode `0x12` plus one payload pointer:

```c
void __thiscall FUN_00432cc0(int param_1, undefined4 param_2)
{
  iVar1 = *(int *)(param_1 + 0x20) + 1;
  if (*(int *)(param_1 + 0x18) < iVar1) {
    FUN_00415510(iVar1);
  }
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = 0x12;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
  ...
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = param_2;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
}
```

`FUN_00432d70` writes opcode `0x15`, the generic FVF path:

```c
void __thiscall FUN_00432d70(int param_1, undefined4 param_2)
{
  iVar1 = *(int *)(param_1 + 0x20) + 1;
  if (*(int *)(param_1 + 0x18) < iVar1) {
    FUN_00415510(iVar1);
  }
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = 0x15;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
  ...
  *(undefined4 *)(*(int *)(param_1 + 0x1c) + *(int *)(param_1 + 0x20) * 4) = param_2;
  *(int *)(param_1 + 0x20) = *(int *)(param_1 + 0x20) + 1;
}
```

Xref counts to the draw-opcode builders:

```text
FUN_00432c70 opcode 0x11: 12 call xrefs
FUN_00432cc0 opcode 0x12: 2 call xrefs
FUN_00432d70 opcode 0x15: 2 call xrefs
```

This xref distribution supports the conclusion that `0x11` is the dominant mesh draw record in the static binary.

## Flush-Side Mapping

`FUN_004342c0` is the renderlist flush/interpreter. Relevant switch cases:

```c
void FUN_004342c0(int param_1)
{
  *(undefined4 *)(param_1 + 0xc) = 0;
  if (*(int *)(param_1 + 8) != 0) {
    do {
      iVar12 = *(int *)(param_1 + 0xc);
      iVar4 = *(int *)(param_1 + 4);
      uVar10 = *(undefined4 *)(iVar4 + iVar12 * 4);
      iVar8 = iVar12 + 1;
      *(int *)(param_1 + 0xc) = iVar8;

      switch(uVar10) {
      ...
      case 0x11:
        _DAT_00a35e64 = _DAT_00a35e64 + 1;
        uVar10 = *(undefined4 *)(*(int *)(param_1 + 4) + *(int *)(param_1 + 0xc) * 4);
        *(int *)(param_1 + 0xc) = *(int *)(param_1 + 0xc) + 1;
        FUN_004540e0(uVar10);
        break;

      case 0x12:
        uVar10 = *(undefined4 *)(iVar4 + iVar8 * 4);
        *(int *)(param_1 + 0xc) = iVar12 + 2;
        FUN_00454660(uVar10);
        break;

      case 0x15:
        uVar10 = *(undefined4 *)(iVar4 + iVar8 * 4);
        *(int *)(param_1 + 0xc) = iVar12 + 2;
        FUN_004507b0(uVar10);
        break;
      ...
      }
    } while (*(int *)(param_1 + 0xc) != *(int *)(param_1 + 8));
  }
}
```

## Draw Paths

### Opcode `0x11` -> `FUN_004540e0` FVF Indexed Path

The `0x11` flush target binds stream 0, sets FVF from the mesh/vertex-buffer wrapper, binds an index buffer, and issues `DrawIndexedPrimitive`.

Relevant decompiled pseudocode:

```c
void __thiscall FUN_004540e0(undefined4 *param_1, uint param_2)
{
  ...
  uVar6 = *(uint *)(uVar5 + 0xc) & 0xfffffffc;

  (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14,0,*(uint *)(uVar6 + 0x24) & 0xfffffffc,0,
         *(uint *)(uVar6 + 0x14) & 0xffff);       // SetStreamSource, vtable 0x190

  iVar8 = *(int *)(uVar6 + 8);
  if (DAT_00a39f14[0x25b1] != iVar8) {
    (**(code **)(*(int *)*DAT_00a39f14 + 0x164))((int *)*DAT_00a39f14,iVar8);
    DAT_00a39f14[0x25b1] = iVar8;                 // SetFVF
    DAT_00a39f14[0x25b2] = 0;
  }

  uVar6 = *(uint *)(uVar5 + 0x10) & 0xfffffffc;
  (**(code **)(*(int *)*DAT_00a39f14 + 0x1a0))
        ((int *)*DAT_00a39f14,*(uint *)(uVar6 + 0x1c) & 0xfffffffc); // SetIndices

  ...
  iVar8 = (**(code **)(*(int *)*param_1 + 0x148))
        ((int *)*param_1,*(undefined4 *)(uVar6 + 8),0,0,
         *(undefined4 *)(*(uint *)(uVar5 + 0xc) & 0xfffffffc),0,iVar8);
}
```

Meaning: this is hookable indexed D3D9 geometry. It is not an UP draw and not pre-transformed screen quad submission.

### Opcode `0x12` -> `FUN_00454660` Multistream Declaration Path

This path uses two streams, `SetVertexDeclaration`, and `DrawIndexedPrimitive`.

Relevant decompiled pseudocode:

```c
void __thiscall FUN_00454660(undefined4 *param_1, int *param_2)
{
  ...
  uVar7 = *(uint *)(*piVar6 + 0xc) & 0xfffffffc;
  (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14,0,*(uint *)(uVar7 + 0x24) & 0xfffffffc,0,
         *(uint *)(uVar7 + 0x14) & 0xffff);       // SetStreamSource stream 0

  uVar7 = *(uint *)(*piVar6 + 0x10) & 0xfffffffc;
  (**(code **)(*(int *)*DAT_00a39f14 + 0x1a0))
        ((int *)*DAT_00a39f14,*(uint *)(uVar7 + 0x1c) & 0xfffffffc); // SetIndices

  (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14,1,piVar6[3] & 0xfffffffc,0,4);        // SetStreamSource stream 1

  iVar1 = piVar6[2];
  if (iVar1 != DAT_00a39f14[0x25b2]) {
    (**(code **)(*(int *)*DAT_00a39f14 + 0x15c))((int *)*DAT_00a39f14,iVar1);
    DAT_00a39f14[0x25b2] = iVar1;                 // SetVertexDeclaration
    DAT_00a39f14[0x25b1] = 0xffffffff;
  }

  ...
  (**(code **)(*(int *)*param_1 + 0x148))
        ((int *)*param_1,*(undefined4 *)((*(uint *)(iVar1 + 0x10) & 0xfffffffc) + 8),
         0,0,*(undefined4 *)(*(uint *)(iVar1 + 0xc) & 0xfffffffc),0,iVar9);
}
```

This is still indexed geometry, but it is not the dominant default branch in `FUN_00884eb0`.

### Opcode `0x15` -> `FUN_004507b0` Generic FVF `0x112` Path

`FUN_004507b0` explicitly sets FVF `0x112` before drawing through the generic indexed wrapper:

```c
void __fastcall FUN_004507b0(undefined4 *param_1)
{
  FUN_00462f20();
  FUN_00462690();
  if (param_1[0x25b1] != 0x112) {
    (**(code **)(*(int *)*param_1 + 0x164))((int *)*param_1,0x112);
    param_1[0x25b1] = 0x112;
    param_1[0x25b2] = 0;
  }
  FUN_0044fc40(0xffffffff,0xffffffff,0xffffffff,0xffffffff);
}
```

`0x112` is `D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1`, i.e. position + normal + one UV set.

`FUN_00462690` confirms that this format is the engine's 3D normal/UV mesh format:

```c
void __fastcall FUN_00462690(int param_1)
{
  if (*(int *)((*(uint *)(param_1 + 0xc) & 0xfffffffc) + 8) == 0x112) {
    ...
    // reads position floats, normal floats, uv floats, transforms/skinning data
    ...
    FUN_00464bb0();
    return;
  }
  FUN_0040c9d0("ERROR - trying to SW skin a mesh with FVF != D3DFVF_3DNUV");
}
```

## Opcode `0x15` Build Side

`FUN_00884980` is called from `FUN_00884eb0` when a Model-level alternate list pointer at `*(param_1 + 0x50) + 0x40` is nonzero. It emits opcode `0x15` for matching submeshes:

```c
void __thiscall FUN_00884980(int param_1, uint param_2)
{
  ...
  FUN_00431540(&DAT_00a0ad38);
  FUN_00432d10();
  ...
  if (0 < *(int *)(*(int *)(iVar4 + 0x30) + 0x28)) {
    iVar4 = 0;
    do {
      if ((*(uint *)(*(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x24) +
            0x38 + iVar4) & 0x1f) == param_2) {
        uVar2 = FUN_00856ec0(0,iVar1,local_44);
        FUN_00431660(uVar2,0);
        FUN_00432d70(*(uint *)(iVar4 +
            *(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x24)) & 0xfffffffc);
      }
      iVar1 = iVar1 + 1;
      iVar4 = iVar4 + 0x3c;
    } while (iVar1 < *(int *)(*(int *)(*(int *)(param_1 + 0x50) + 0x30) + 0x28));
  }
  FUN_00432d40();
}
```

This is still a fixed-function FVF path (`0x112`), so it is not evidence against Remix-compatible 3D indexed geometry.

## Vertex Declarations And Shaders

`FUN_00461d30` creates vertex declarations. The declaration elements include position, normal, UV, and additional stream/blend-like data. The calls are all to `CreateVertexDeclaration` at vtable offset `0x158`.

Relevant decompiled pseudocode:

```c
void FUN_00461d30(void)
{
  local_c0 = 0;
  local_be = 0;
  local_bc = 2;       // FLOAT3-like type
  ...
  local_b8 = 0;
  local_b6 = 0xc;
  local_b4 = 2;       // normal FLOAT3-like type
  ...
  local_b0 = 0;
  local_ae = 0x18;
  local_ac = 1;       // FLOAT2-like UV type
  ...
  (**(code **)(*(int *)*DAT_00a39f14 + 0x158))
        ((int *)*DAT_00a39f14,&local_c0,&DAT_00a39f40);

  ...
  (**(code **)(*(int *)*DAT_00a39f14 + 0x158))
        ((int *)*DAT_00a39f14,&local_a4,&DAT_00a39f44);

  ...
  (**(code **)(*(int *)*DAT_00a39f14 + 0x158))
        ((int *)*DAT_00a39f14,&uStack_80,&DAT_00a39f48);

  ...
  (**(code **)(*(int *)*DAT_00a39f14 + 0x158))
        ((int *)*DAT_00a39f14,&uStack_5c,&DAT_00a39f4c);
}
```

Instruction-search control checks found the expected D3D9 COM calls:

```text
CALL vtable +0x158 CreateVertexDeclaration:
  00461deb, 00461eb0, 00461fb7, 0046211c

CALL vtable +0x15c SetVertexDeclaration:
  0044d208, 00454ad7

CALL vtable +0x164 SetFVF:
  0044d1c8, 004507db, 004513ad, 004515da, 0045187d, 00451ab8,
  00451d7d, 0045216c, 00452638, 00453ca1, 00453dec, 00454096,
  004543b3, 00454cd1

CALL vtable +0x148 DrawIndexedPrimitive:
  0044fcb1, 00454554, 00454b44
```

Shader-call searches:

```text
CALL vtable +0x16c CreateVertexShader: 0 matches
CALL vtable +0x170 SetVertexShader:    0 matches
CALL vtable +0x1a8 CreatePixelShader:  0 matches
CALL vtable +0x1ac SetPixelShader:     0 matches

Additional shader-constant checks:
CALL vtable +0x178 SetVertexShaderConstantF: 0 matches
CALL vtable +0x180 SetVertexShaderConstantI: 0 matches
CALL vtable +0x188 SetVertexShaderConstantB: 0 matches
CALL vtable +0x1b4 SetPixelShaderConstantF:  0 matches
CALL vtable +0x1bc SetPixelShaderConstantI:  0 matches
CALL vtable +0x1c4 SetPixelShaderConstantB:  0 matches
```

Conclusion: I found declarations, but no real programmable vertex or pixel shader creation/binding in `TOD.exe`. Therefore `FUN_00454660` should be treated as a declaration/multistream draw path, not as proven programmable-shader rendering.

## FVF vs Declaration Dominance

Evidence favoring static-world FVF dominance:

1. In `FUN_00884eb0`, the default draw branch is:

```c
if (iVar13 == 0) {
  FUN_00432c70(local_13c);   // opcode 0x11
}
else {
  FUN_00432cc0(iVar13);      // opcode 0x12
}
```

2. The `0x12` branch requires `FUN_0085e690(...)` to return a nonzero alternate draw resource:

```c
uint __thiscall FUN_0085e690(int param_1,int param_2)
{
  if (param_2 == -1) {
    return 0;
  }
  return *(uint *)(*(int *)(param_1 + 0x20) + param_2 * 0x18) & 0xfffffffc;
}
```

The call to `FUN_0085e690` itself is gated by several Model flags and per-submesh map checks in `FUN_00884eb0`, including `param_1 + 0xa4`, `param_1 + 0xa0`, `param_1 + 0xb0`, and mesh flag bit checks. That makes `0x12` a special/alternate path, not the default static mesh path. The exact semantic name of the gate remains uncertain because the involved structs are not typed.

3. Builder xrefs heavily favor `FUN_00432c70` (`0x11`) over `FUN_00432cc0` (`0x12`) and `FUN_00432d70` (`0x15`).

Final path assessment:

```text
Static/default Model world geometry:
  FUN_00884eb0 -> FUN_00432c70 -> opcode 0x11
  FUN_004342c0 -> FUN_004540e0
  SetStreamSource + SetFVF + SetIndices + DrawIndexedPrimitive

Special/alternate multistream geometry:
  FUN_00884eb0 -> FUN_00432cc0 -> opcode 0x12
  FUN_004342c0 -> FUN_00454660
  SetStreamSource stream 0 + SetStreamSource stream 1 + SetVertexDeclaration + DrawIndexedPrimitive

Alternate/generic 3DNUV path:
  FUN_00884980 -> FUN_00432d70 -> opcode 0x15
  FUN_004342c0 -> FUN_004507b0
  SetFVF 0x112 + DrawIndexedPrimitive wrapper
```

## RTX Remix Implication

Finding 1 from the prior pass remains intact and is strengthened here: real indexed 3D geometry reaches D3D9. The Model render method now has a confirmed path into the `0x11` fixed-function FVF indexed draw path.

The main static-world path is favorable for Remix: it submits vertex/index buffers via standard D3D9 indexed draws and uses 3D FVF data, not pre-transformed `XYZRHW` quads. The declaration path exists, but I found no real vertex/pixel shader API usage in `TOD.exe`, and the Model renderer uses that path only through a gated alternate-resource branch.
