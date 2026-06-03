# Total Overdose RTX Remix RE Findings

Program loaded in Ghidra: `TOD.exe`

Scope: read-only reverse engineering of D3D9 submission paths. This report cites Ghidra addresses and decompiled pseudocode for each function used as evidence.

## Task 1 - Locate the Adapter / D3D9 Backend

Status: complete.

`TOD.exe` contains the D3D9 backend. No renderer DLL was needed for this step.

### Key Addresses

- `Direct3DCreate9` thunk: `0099e9f2`
- `Direct3DCreate9` IAT/data pointer: `009b32f8`
- Adapter initialization / constructor-like function: `FUN_0045e620`
- `IDirect3D9::CreateDevice` call function: `FUN_00451110`
- Adapter singleton/global pointer: `DAT_00a39f14` at `00a39f14`
- `IDirect3D9 *` storage: adapter offset `0x40` (`param_1[0x10]`)
- `IDirect3DDevice9 *` storage: adapter offset `0x00` (`*param_1`)

### Evidence: Direct3DCreate9 and Adapter Storage

```c
// FUN_0045e620 @ 0045e620
undefined4 * __thiscall FUN_0045e620(undefined4 *param_1)
{
    ...

    iVar5 = Direct3DCreate9(0x20);
    param_1[0x10] = iVar5;              // this + 0x40 = IDirect3D9*

    if (iVar5 == 0) {
        FUN_0043c040(4,0);
    }

    ...

    DAT_00a39f14 = param_1;             // global adapter pointer

    iVar5 = (**(code **)(*(int *)param_1[0x10] + 0x38))
        ((int *)param_1[0x10], 0, 1, param_1 + 0x11);

    if (iVar5 < 0) {
        FUN_0043c040(3,0);
    }

    ...

    if ((unaff_retaddr & 2) == 0) {
        FUN_0045bef0(param_1[0xca], param_1[0xcb]);
    }
    else {
        FUN_0045be30(param_1[0xca], param_1[0xcb]);
    }

    ...
    return param_1;
}
```

### Evidence: IDirect3D9::CreateDevice

```c
// FUN_00451110 @ 00451110
void __fastcall FUN_00451110(int *param_1)
{
    int *piVar1;
    undefined4 uVar2;

    ...

    piVar1 = (int *)*param_1;           // this + 0x00 = old IDirect3DDevice9*
    if (piVar1 != (int *)0x0) {
        (**(code **)(*piVar1 + 8))(piVar1);  // Release()
        *param_1 = 0;
    }

    if ((param_1[0x18] & 0x10000U) == 0) {
        FUN_0040c9d0("Creating a SW device.\n");
        uVar2 = 0x20;                   // D3DCREATE_SOFTWARE_VERTEXPROCESSING
    }
    else {
        FUN_0040c9d0("Creating a HW device.\n");
        uVar2 = 0x40;                   // D3DCREATE_HARDWARE_VERTEXPROCESSING
    }

    (**(code **)(*(int *)param_1[0x10] + 0x40))
        ((int *)param_1[0x10],          // IDirect3D9*
         0,
         1,
         *(undefined4 *)(DAT_00a35eb8 + 0x24),
         uVar2,
         param_1 + 0x5e,
         param_1);                      // IDirect3DDevice9** -> this + 0x00

    ...
}
```

Assembly confirmation:

```asm
004511b0  MOV EAX,dword ptr [ESI + 0x40]   ; IDirect3D9*
004511b3  MOV EDX,dword ptr [EAX]          ; vtable
...
004511d0  PUSH EAX                         ; this
004511d1  CALL dword ptr [EDX + 0x40]      ; IDirect3D9::CreateDevice
```

### Task 1 Conclusion

The Adapter/PC D3D9 backend is in `TOD.exe`, centered around `FUN_0045e620` for initialization and `FUN_00451110` for `IDirect3D9::CreateDevice`. The D3D device pointer is stored directly at offset `0x00` of the adapter object, whose global pointer is `DAT_00a39f14`.

## Task 2 - Breadcrumb via Model Strings

Status: complete.

The known Model property strings are present. The canonical Model registration function is `FUN_00889800`.

### String Addresses

| String | Address | Notes |
| --- | ---: | --- |
| `hard_alpha_factor` | `009cd060` | Xref from `FUN_00889800` |
| `use_hard_alpha_factor` | `009cd074` | Xref from `FUN_00889800` |
| `active_texture_set` | `009c2590` | String starts after a `59` prefix at `009c258e`; xref from `FUN_00889800` |
| `number_of_textures_sets` | `009cd02c` | Xref from `FUN_00889800` |
| `dynamically_lit` | `009c5578` | Xrefs from `FUN_00889800` and other classes |
| `statically_lit` | `009ccfc0` | Xref from `FUN_00889800` |
| `place_in_hud` | `009cd010` | Xref from `FUN_00889800` |
| `single_color_mode` | `009ccf94` | Xref from `FUN_00889800` |
| `backside_transparent` | `009ccfa8` | Xref from `FUN_00889800` |
| `addblend` | `009cd09c` | Xref from `FUN_00889800` |
| `disablezwrite` | `009cd08c` | Xref from `FUN_00889800` |
| `opacity` | `009c25a4` | Xref from `FUN_00889800` and others |
| `solo_pivot` | `009cd020` | Xref from `FUN_00889800` |

### Evidence: Model Registration

```c
// FUN_00889800 @ 00889800
void FUN_00889800(void)
{
    if (DAT_00a3afbc == '\0') {
        iVar1 = (**(code **)(*DAT_00a3afc0 + 8))(0x78,&DAT_009b3356,0);
        if (iVar1 != 0) {
            DAT_00a3d848 = FUN_0086cc00("Model");
            goto LAB_00889835;
        }
    }
    DAT_00a3d848 = 0;

LAB_00889835:
    FUN_0086cb40(DAT_00a3d884);
    *(undefined1 **)(DAT_00a3d848 + 0x20) = &LAB_0088aa70;

    FUN_0086db90("modelres", DAT_00a3ceb0, &LAB_00884350, 0, 0, 0,
                 FUN_00888e10, 0, 0, 0, "control=resource|type=*.model", 10);
    FUN_0086d930("opacity", DAT_00a3cec0, FUN_00883f90, 0, 0, 0,
                 FUN_00883fb0, 0, 0, 0, "control=slider|min=0|max=1", 0xc);
    FUN_0086de50("addblend", DAT_00a3cec4, &LAB_00883d30, 0, 0, 0,
                 &LAB_00883d40, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("disablezwrite", DAT_00a3cec4, &LAB_00883d70, 0, 0, 0,
                 &LAB_00883d80, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("use_hard_alpha_factor", DAT_00a3cec4, &LAB_00883cf0,
                 0, 0, 0, &LAB_00883d00, 0, 0, 0, "control=truth|name=");
    FUN_0086d990("hard_alpha_factor", DAT_00a3cec0, &LAB_00884030,
                 0, 0, 0, &LAB_00884050, 0, 0, 0,
                 "control=slider|min=0|max=1");
    FUN_0086d6d0("active_texture_set", DAT_00a3ceb8, FUN_00884140,
                 0, 0, 0, FUN_00884150, 0, 0, 0,
                 "control=slider|min=0|max=32", 0xb);
    FUN_0086d730("number_of_textures_sets", DAT_00a3ceb8, FUN_008844f0,
                 0, 0, 0, 0, 0, 0, 0xffffffff, "control=string");
    FUN_0086d730("solo_pivot", DAT_00a3ceb8, &LAB_00883db0,
                 0, 0, 0, &LAB_00883ed0, 0, 0, 0,
                 "control=slider|min=0|max=32");
    FUN_0086de50("place_in_hud", DAT_00a3cec4, &LAB_00883df0,
                 0, 0, 0, &LAB_00883dc0, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("dynamically_lit", DAT_00a3cec4, &LAB_008840a0,
                 0, 0, 0, FUN_008840b0, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("statically_lit", DAT_00a3cec4, FUN_00884100,
                 0, 0, 0, FUN_0065d3a0, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("backside_transparent", DAT_00a3cec4, &LAB_00884110,
                 0, 0, 0, &LAB_00884120, 0, 0, 0, "control=truth|name=");
    FUN_0086de50("single_color_mode", DAT_00a3cec4, &LAB_00883e70,
                 0, 0, 0, &LAB_00883ef0, 0, 0, 0, "control=truth|name=");

    FUN_0086edc0("findpivot(string):integer", &LAB_0088a110, 0, 0, 0);
    FUN_0086edc0("setmodelresfrommodel(entity)", &LAB_0088aab0, 0, 0, 0);
    FUN_0086edc0("getpivotrelativepos(integer):vector", FUN_0088a250, 0, 0, 0);
    FUN_0086edc0("forceinstantiate", &LAB_00883eb0, 0, 0, 0);
    FUN_0086e9b0();
}
```

### Evidence: Model Constructor and Vtable

The class factory stored at `DAT_00a3d848 + 0x20` allocates `0x100` bytes and jumps to `FUN_00884ba0`. The constructor installs two vtable-like pointers.

```c
// FUN_00884ba0 @ 00884ba0
undefined4 * __fastcall FUN_00884ba0(undefined4 *param_1)
{
    FUN_0088d4b0(7);
    *param_1 = &PTR_FUN_009cce48;
    param_1[9] = &PTR_FUN_009ce800;

    param_1[0x14] = 0;
    param_1[0x15] = 1;
    param_1[0x16] = 0;
    param_1[0x17] = 1;

    ...

    param_1[0x18] = 0;
    param_1[10] = param_1[10] | 0xf000;
    *(byte *)(param_1 + 5) = *(byte *)(param_1 + 5) | 8;
    param_1[0x1b] = param_1[0x1b] & 0x806440ff | 0x640000;
    param_1[0x1d] = 0;
    param_1[0x1c] = param_1[0x1c] & 0xff2fffff | 0xf200000;
    return param_1;
}
```

`PTR_FUN_009cce48 + 0x20` points to `00884eb0`, and scene traversal calls virtual slot `+0x20` on nodes/models. Ghidra has not defined `00884eb0` as a function, so I cannot provide decompiler pseudocode for it without modifying the project. The identity of this exact virtual as the Model render method is therefore marked probable, not fully proven from decompiler output alone.

### Evidence: Scene Traversal Uses Virtual Slot +0x20

```c
// FUN_0088c310 @ 0088c310
void __thiscall FUN_0088c310(int *param_1, undefined4 param_2)
{
    uVar1 = param_1[10];
    if (((((uVar1 >> 0x10 | uVar1) & 1) == 0) &&
        (((DAT_00a3d891 == '\0' || ((uVar1 & 0x10000000) == 0)) ||
          (DAT_00a3d892 == '\0')))) &&
        ((uVar1 & 4) == 0)) {

        if ((*(byte *)(param_1 + 5) & 1) != 0) {
            (**(code **)(*param_1 + 0x14))();
            *(byte *)(param_1 + 5) = *(byte *)(param_1 + 5) & 0xfe;
        }

        iVar2 = param_1[0xc];
        if (iVar2 != 0) {
            uVar3 = FUN_00951640();
            *(undefined4 *)(iVar2 + 0x54) = uVar3;
            FUN_008a1310(param_2,
                         (int)(*(float *)(param_1[0xc] + 0x4c) *
                               DAT_009b355c + DAT_009b33ec));
        }

        (**(code **)(*param_1 + 0x20))();   // likely Model render/draw virtual
    }

    for (iVar2 = param_1[0x11]; iVar2 != 0; iVar2 = *(int *)(iVar2 + 0x34)) {
        FUN_0088c310(param_2);
    }
}
```

```c
// FUN_00893d10 @ 00893d10
void __fastcall FUN_00893d10(int param_1)
{
    ...

    if (0 < *(int *)(param_1 + 0xe0)) {
        do {
            piVar7 = *(int **)(*(int *)(param_1 + 0xdc) + iStack_88 * 4);
            piVar1 = (int *)*piVar7;

            if ((*(byte *)(piVar1 + 10) & 2) == 0) {
                uVar2 = piVar1[10];
                if ((((uVar2 >> 0x10 | uVar2) & 1) == 0) && ((uVar2 & 4) == 0)) {
                    if ((*(byte *)(piVar1 + 5) & 1) != 0) {
                        (**(code **)(*piVar1 + 0x14))();
                        FUN_0088c2f0();
                    }

                    if ((piVar1[0xc] == 0) ||
                        (*(float *)(piVar1[0xc] + 0x4c) < _DAT_009b4c64)) {
                        iVar5 = FUN_00951640();
                        fVar8 = (float)piVar7[0x13] * DAT_009b355c + DAT_009b33ec;
                        piVar7[0x15] = iVar5;
                        FUN_008a1310(puVar4, (int)fVar8);
                    }

                    (**(code **)(*piVar1 + 0x20))();   // likely render/draw virtual
                }
            }

            ...
        } while (iStack_88 < *(int *)(param_1 + 0xe0));
    }
}
```

### Task 2 Conclusion

`FUN_00889800` is the Model class Register/property setup. `FUN_00884ba0` is the Model constructor. The likely Model render/draw method is the vtable slot `PTR_FUN_009cce48 + 0x20`, target `00884eb0`, because render traversal calls `(*vtable + 0x20)` for each visible node/model. This exact function remains uncertain at the decompiler level because Ghidra has not created a function at `00884eb0`.

## Task 3 - Find the Draw Submission

Status: complete.

Instruction search results for `IDirect3DDevice9` draw slots:

- `DrawPrimitive` slot index 81, vtable offset `0x144`: 11 call sites.
- `DrawIndexedPrimitive` slot index 82, vtable offset `0x148`: 3 call sites.
- `DrawPrimitiveUP` slot index 83, vtable offset `0x14c`: 0 call sites found.
- `DrawIndexedPrimitiveUP` slot index 84, vtable offset `0x150`: 0 call sites found.

The renderlist flush function is `FUN_004342c0`. It iterates an opcode stream/list and dispatches render commands to adapter functions. The world mesh opcodes of interest are:

- opcode `0x11`: `FUN_004540e0(...)`, RenderMeshBuffer indexed draw path.
- opcode `0x12`: `FUN_00454660(...)`, skinned or multi-stream indexed draw path.
- opcode `0x15`: `FUN_004507b0(...)`, generic indexed geometry path that sets FVF `0x112` then calls `FUN_0044fc40`.

### Evidence: Renderlist Flush

```c
// FUN_004342c0 @ 004342c0
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

            switch (uVar10) {
            ...

            case 0x11:
                _DAT_00a35e64 = _DAT_00a35e64 + 1;
                uVar10 = *(undefined4 *)(*(int *)(param_1 + 4) +
                                          *(int *)(param_1 + 0xc) * 4);
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

### Evidence: Generic Indexed Draw

```c
// FUN_0044fc40 @ 0044fc40
void __thiscall
FUN_0044fc40(undefined4 *param_1, undefined4 param_2, int param_3,
             undefined4 param_4, int param_5)
{
    if (param_3 < 0) {
        param_5 = *(int *)param_1[0x25bd];
        param_3 = *(int *)param_1[0x25bc];
        param_4 = 0;
        param_2 = 0;
    }

    if (*(int *)(param_1[0x25bc] + 4) == 1) {
        param_3 = param_3 / 3;
    }
    else {
        param_3 = param_3 + -2;
    }

    FUN_0044f8a0();

    iVar1 = (**(code **)(*(int *)*param_1 + 0x148))
        ((int *)*param_1,
         *(undefined4 *)(param_1[0x25bc] + 8),
         0, param_4, param_5, param_2, param_3);

    if (iVar1 < 0) {
        FUN_0040c9d0("RenderIndexedGeometry::DrawIndexedPrimitive(%i, 0, %i, %i, %i, %i) FAILED\n",
                     *(undefined4 *)(param_1[0x25bc] + 8),
                     param_4, param_5, param_2, param_3);
    }

    param_1[3] = param_1[3] + 1;
    param_1[5] = param_1[5] + param_5;
    param_1[4] = param_1[4] + param_3;
}
```

### Evidence: RenderMeshBuffer Indexed Draw

```c
// FUN_004540e0 @ 004540e0
void __thiscall FUN_004540e0(undefined4 *param_1, uint param_2)
{
    ...

    uVar6 = *(uint *)(uVar5 + 0xc) & 0xfffffffc;

    // IDirect3DDevice9::SetStreamSource, offset 0x190.
    (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14,
         0,
         *(uint *)(uVar6 + 0x24) & 0xfffffffc,
         0,
         *(uint *)(uVar6 + 0x14) & 0xffff);

    iVar8 = *(int *)(uVar6 + 8);
    local_10 = DAT_00a39f14 + 0x25b1;
    if (DAT_00a39f14[0x25b1] != iVar8) {
        // IDirect3DDevice9::SetFVF, offset 0x164.
        (**(code **)(*(int *)*DAT_00a39f14 + 0x164))
            ((int *)*DAT_00a39f14, iVar8);
        *local_10 = iVar8;
        DAT_00a39f14[0x25b2] = 0;
    }

    DAT_00a39f14[0x25bd] = uVar6;
    uVar6 = *(uint *)(uVar5 + 0x10) & 0xfffffffc;

    // IDirect3DDevice9::SetIndices, offset 0x1a0.
    (**(code **)(*(int *)*DAT_00a39f14 + 0x1a0))
        ((int *)*DAT_00a39f14, *(uint *)(uVar6 + 0x1c) & 0xfffffffc);

    DAT_00a39f14[0x25bc] = uVar6;

    ...

    FUN_0044f8a0();

    iVar8 = (**(code **)(*(int *)*param_1 + 0x148))
        ((int *)*param_1,
         *(undefined4 *)(uVar6 + 8),
         0,
         0,
         *(undefined4 *)(*(uint *)(uVar5 + 0xc) & 0xfffffffc),
         0,
         iVar8);

    if (iVar8 < 0) {
        FUN_0040c9d0("RenderMeshBuffer::DrawIndexedPrimitive(%i, 0, 0, %i, 0, %i) FAILED\n",
                     *(undefined4 *)(uVar6 + 8),
                     *(undefined4 *)(*(uint *)(uVar5 + 0xc) & 0xfffffffc),
                     iVar8);
    }

    ...
}
```

### Evidence: Skinned/Multi-Stream Indexed Draw

```c
// FUN_00454660 @ 00454660
void __thiscall FUN_00454660(undefined4 *param_1, int *param_2)
{
    ...

    uVar7 = *(uint *)(*piVar6 + 0xc) & 0xfffffffc;

    // SetStreamSource(0, vertexBuffer, 0, stride)
    (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14,
         0,
         *(uint *)(uVar7 + 0x24) & 0xfffffffc,
         0,
         *(uint *)(uVar7 + 0x14) & 0xffff);

    DAT_00a39f14[0x25bd] = uVar7;
    uVar7 = *(uint *)(*piVar6 + 0x10) & 0xfffffffc;

    // SetIndices(...)
    (**(code **)(*(int *)*DAT_00a39f14 + 0x1a0))
        ((int *)*DAT_00a39f14, *(uint *)(uVar7 + 0x1c) & 0xfffffffc);

    DAT_00a39f14[0x25bc] = uVar7;

    // SetStreamSource(1, secondaryBuffer, 0, 4)
    (**(code **)(*(int *)*DAT_00a39f14 + 400))
        ((int *)*DAT_00a39f14, 1, piVar6[3] & 0xfffffffc, 0, 4);

    puVar5 = DAT_00a39f14;
    iVar1 = piVar6[2];
    piVar8 = DAT_00a39f14 + 0x25b2;
    if (iVar1 != DAT_00a39f14[0x25b2]) {
        // SetVertexDeclaration(...)
        (**(code **)(*(int *)*DAT_00a39f14 + 0x15c))
            ((int *)*DAT_00a39f14, iVar1);
        *piVar8 = iVar1;
        puVar5[0x25b1] = 0xffffffff;
    }

    FUN_0044f8a0();

    (**(code **)(*(int *)*param_1 + 0x148))
        ((int *)*param_1,
         *(undefined4 *)((*(uint *)(iVar1 + 0x10) & 0xfffffffc) + 8),
         0,
         0,
         *(undefined4 *)(*(uint *)(iVar1 + 0xc) & 0xfffffffc),
         0,
         iVar9);

    ...
}
```

### Task 3 Conclusion

`FUN_004342c0` is the renderlist flush/interpreter. It dispatches world mesh draw opcodes to adapter functions that call `IDirect3DDevice9::DrawIndexedPrimitive` through vtable offset `0x148`. There are no `DrawPrimitiveUP` or `DrawIndexedPrimitiveUP` call sites found.

## Task 4 - Deciding Question: Vertex Format

Status: complete.

The world mesh path submits real 3D geometry. The main FVF is `0x112`, which is:

- `D3DFVF_XYZ` = `0x002`
- `D3DFVF_NORMAL` = `0x010`
- `D3DFVF_TEX1` = `0x100`
- Combined: `0x112`

The binary itself labels this format `D3DFVF_3DNUV` in the SW skinning error string.

Important caveat: XYZRHW formats are present in the adapter's vertex format table for screen-space/simple primitive paths, and those likely explain the prior fullscreen-quad-only capture. They are not the world mesh `RenderMeshBuffer` indexed path.

### Evidence: Vertex Format Table

`FUN_00464e70` uses a vertex format table at `DAT_00a0abd0` / `DAT_00a0abd4`.

```c
// FUN_00464e70 @ 00464e70
int * __thiscall FUN_00464e70(int *param_1, int param_2, int *param_3, int param_4)
{
    param_1[4] = 0;
    param_1[9] = 0;
    *(undefined1 *)((int)param_1 + 0x16) = 0;

    *param_1 = (int)param_3;
    *(undefined2 *)(param_1 + 5) = *(undefined2 *)(&DAT_00a0abd0 + param_2 * 8); // stride
    param_1[2] = *(int *)(&DAT_00a0abd4 + param_2 * 8);                         // FVF

    iVar2 = (param_1[5] & 0xffffU) * (int)param_3;
    param_1[3] = iVar2;
    param_1[1] = 0;
    param_1[7] = param_4;
    param_1[6] = param_2;
    param_1[8] = 0;

    ...

    FUN_00464cc0();
    param_3 = param_1;
    FUN_00465660(local_8, &param_3);
    return param_1;
}
```

Raw table entries at `00a0abd0` begin:

| Format index | Stride | FVF | Interpretation |
| ---: | ---: | ---: | --- |
| 0 | `0x14` | `0x044` | XYZRHW + diffuse, screen-space |
| 1 | `0x18` | `0x104` | XYZRHW + TEX1, screen-space |
| 2 | `0x1c` | `0x144` | XYZRHW + diffuse + TEX1, screen-space |
| 3 | `0x18` | `0x012` | XYZ + normal |
| 4 | `0x10` | `0x042` | XYZ + diffuse |
| 5 | `0x1c` | `0x052` | XYZ + normal + diffuse |
| 6 | `0x20` | `0x112` | XYZ + normal + TEX1, real 3D mesh |
| 7 | `0x28` | `0x212` | XYZ + normal + TEX2 |
| 8 | `0x18` | `0x142` | XYZ + diffuse + TEX1 |
| 9 | `0x24` | `0x152` | XYZ + normal + diffuse + TEX1 |

### Evidence: Vertex Buffer Creation Uses FVF

```c
// FUN_00464cc0 @ 00464cc0
void __fastcall FUN_00464cc0(int param_1)
{
    uVar3 = 8;
    if ((*(byte *)(param_1 + 0x1c) & 1) != 0) {
        uVar3 = 0x208;
    }

    local_4 = param_1;

    // IDirect3DDevice9::CreateVertexBuffer, offset 0x68.
    (**(code **)(*(int *)*DAT_00a39f14 + 0x68))
        ((int *)*DAT_00a39f14,
         *(undefined4 *)(param_1 + 0xc),    // byte size
         uVar3,                             // usage
         *(undefined4 *)(param_1 + 8),      // FVF
         0,
         &local_4,
         0);

    ...

    *(uint *)(param_1 + 0x24) = uVar3 | 1;  // decompiler aliases out pointer state here
}
```

The decompiler aliases the out pointer variable in the tail, but the call argument order is consistent with `CreateVertexBuffer(size, usage, fvf, pool, ppVertexBuffer, sharedHandle)`.

### Evidence: SW Skinning Requires D3DFVF_3DNUV

```c
// FUN_00462690 @ 00462690
void __fastcall FUN_00462690(int param_1)
{
    if (*(int *)((*(uint *)(param_1 + 0xc) & 0xfffffffc) + 8) == 0x112) {
        iVar8 = FUN_00464b60(3);
        local_128 = 0;

        if (0 < *(int *)(*(uint *)(param_1 + 0xc) & 0xfffffffc)) {
            local_14c = 0;
            pfVar13 = (float *)(iVar8 + 8);
            do {
                uVar10 = *(uint *)(param_1 + 0x44) & 0xfffffffc;

                // source vertex position
                fVar1 = *(float *)(uVar10 + local_14c);
                fVar2 = *(float *)(uVar10 + 4 + local_14c);
                fVar3 = *(float *)(uVar10 + 8 + local_14c);

                // source vertex normal
                iVar11 = uVar10 + local_14c;
                fVar4 = *(float *)(iVar11 + 0xc);
                fVar5 = *(float *)(iVar11 + 0x10);
                fVar6 = *(float *)(iVar11 + 0x14);

                ...

                // output skinned position
                pfVar13[-2] = local_148;
                pfVar13[-1] = local_144;
                *pfVar13 = local_140;

                // output skinned normal
                pfVar13[1] = local_138;
                pfVar13[2] = local_134;
                pfVar13[3] = local_130;

                // copy UV
                pfVar13[4] = *(float *)(iVar11 + 0x18);
                pfVar13[5] = *(float *)(iVar11 + 0x1c);

                local_14c = local_14c + 0x60;
                local_128 = local_128 + 1;
                pfVar13 = pfVar13 + 8;
            } while (local_128 < *(int *)(*(uint *)(param_1 + 0xc) & 0xfffffffc));
        }

        FUN_00464bb0();
        return;
    }

    FUN_0040c9d0("ERROR - trying to SW skin a mesh with FVF != D3DFVF_3DNUV");
}
```

This is strong evidence that the model mesh input format is 3D position + normal + UV, not pre-transformed RHW.

### Evidence: Generic Indexed Geometry Forces FVF 0x112

```c
// FUN_004507b0 @ 004507b0
void __fastcall FUN_004507b0(undefined4 *param_1)
{
    FUN_00462f20();
    FUN_00462690();

    if (param_1[0x25b1] != 0x112) {
        // IDirect3DDevice9::SetFVF(0x112)
        (**(code **)(*(int *)*param_1 + 0x164))((int *)*param_1, 0x112);
        param_1[0x25b1] = 0x112;
        param_1[0x25b2] = 0;
    }

    FUN_0044fc40(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
}
```

### Evidence: World Transform is Submitted to D3D

```c
// FUN_0044def0 @ 0044def0
void __thiscall FUN_0044def0(undefined4 *param_1, undefined4 *param_2)
{
    // IDirect3DDevice9::SetTransform(D3DTS_WORLD, matrix)
    (**(code **)(*(int *)*param_1 + 0xb0))((int *)*param_1, 0x100, param_2);

    param_1[0x1589] = *param_2;
    param_1[0x158a] = param_2[1];
    param_1[0x158b] = param_2[2];
    param_1[0x158c] = param_2[3];
    param_1[0x158d] = param_2[4];
    param_1[0x158e] = param_2[5];
    param_1[0x158f] = param_2[6];
    param_1[0x1590] = param_2[7];
    param_1[0x1591] = param_2[8];
    param_1[0x1592] = param_2[9];
    param_1[0x1593] = param_2[10];
    param_1[0x1594] = param_2[0xb];
    param_1[0x1595] = param_2[0xc];
    param_1[0x1596] = param_2[0xd];
    param_1[0x1597] = param_2[0xe];
    param_1[0x1598] = param_2[0xf];
}
```

```c
// FUN_00454d70 @ 00454d70, relevant tail
void __thiscall FUN_00454d70(undefined4 *param_1, undefined4 param_2)
{
    ...

LAB_0045a7bd:
    FUN_0044def0(&local_50);  // sets D3D world transform

    if (*(char *)(param_1 + 0x25cc) != '\0') {
        FUN_004540e0(param_1[0x25d5]);
        return;
    }

    FUN_0045ed30(1, *(undefined1 *)(param_1 + 0xb3));

    ...

    (**(code **)(*(int *)*param_1 + 0xc4))((int *)*param_1, local_98);
    FUN_004540e0(param_1[0x25d4]);

    ...
}
```

### Evidence: Vertex Declarations Also Contain Position, Normal, UV

The skinned/multistream path uses `SetVertexDeclaration`. `FUN_00461d30` creates those declarations. The first declaration contains stream 0 float3 position, stream 0 float3 normal, stream 0 float2 texcoord, plus stream 1 blend/index data.

```c
// FUN_00461d30 @ 00461d30
void FUN_00461d30(void)
{
    local_c0 = 0;       // stream 0
    local_be = 0;       // offset 0
    local_bc = 2;       // D3DDECLTYPE_FLOAT3
    local_bb = 0;
    local_ba = 0;       // D3DDECLUSAGE_POSITION
    local_b9 = 0;

    local_b8 = 0;       // stream 0
    local_b6 = 0xc;     // offset 12
    local_b4 = 2;       // D3DDECLTYPE_FLOAT3
    local_b3 = 0;
    local_b2 = 3;       // D3DDECLUSAGE_NORMAL
    local_b1 = 0;

    local_b0 = 0;       // stream 0
    local_ae = 0x18;    // offset 24
    local_ac = 1;       // D3DDECLTYPE_FLOAT2
    local_ab = 0;
    local_aa = 5;       // D3DDECLUSAGE_TEXCOORD
    local_a9 = 0;

    local_a8 = 1;       // stream 1
    local_a6 = 0;
    local_a4 = 4;
    local_a2 = 10;

    local_a0 = 0xff;    // D3DDECL_END
    local_9e = 0;
    local_9c = 0x11;
    local_9a = 0;

    // IDirect3DDevice9::CreateVertexDeclaration, offset 0x158.
    (**(code **)(*(int *)*DAT_00a39f14 + 0x158))
        ((int *)*DAT_00a39f14, &local_c0, &DAT_00a39f40);

    ...
}
```

### Task 4 Conclusion

XYZRHW is present in the adapter for screen-space paths (`0x044`, `0x104`, `0x144` table entries), but the world mesh indexed path uses real 3D formats:

- FVF mesh path: `0x112` / `D3DFVF_3DNUV` = XYZ + normal + UV.
- Vertex declaration path: stream 0 float3 position + float3 normal + float2 texcoord.
- World transforms are sent to D3D via `SetTransform(D3DTS_WORLD, ...)`.
- Geometry is submitted through real vertex buffers, index buffers, `SetStreamSource`, `SetIndices`, and `DrawIndexedPrimitive`.

## Task 5 - Verdict

Status: complete.

### Finding

Finding 1 is supported.

Real 3D indexed geometry is submitted via the Adapter's D3D9 backend. The load-bearing draw site is `FUN_004540e0` at `004540e0`, called by renderlist flush `FUN_004342c0` opcode `0x11`. It binds a real vertex buffer with `SetStreamSource`, sets FVF from the mesh buffer, binds a real index buffer with `SetIndices`, and calls `IDirect3DDevice9::DrawIndexedPrimitive` at vtable offset `0x148`.

For the generic/skin path, `FUN_004507b0` explicitly sets FVF `0x112` before calling `FUN_0044fc40`, which performs `DrawIndexedPrimitive`. `FUN_00462690` confirms `0x112` is the engine's `D3DFVF_3DNUV` format and shows position, normal, and UV data being used.

### Confidence

High confidence on the D3D9 backend, draw call type, vertex/index buffer use, and `0x112` real 3D FVF evidence.

Medium confidence on the exact Model virtual method address because Ghidra has not created a decompilable function at vtable target `00884eb0`. The surrounding class registration, constructor/vtable, traversal, and downstream renderlist flush evidence are consistent with that slot being Model render/draw, but the exact function body remains only disassembly-backed in this pass.

### Remix Implication

This is not a pure pre-transformed `XYZRHW` renderer. The engine does have XYZRHW paths for screen-space/simple primitives, but world mesh geometry reaches D3D9 as hookable indexed primitives with 3D vertex formats. If RTX Remix is not seeing the world, the evidence points more toward hook/config/timing/render-target/composite issues than an absence of reconstructable 3D geometry at the D3D9 boundary.

## Task 6 - Camera Transform Path

Status: complete.

### Finding

Follow-up Ghidra MCP inspection found the D3D9 camera transform path. The game does submit explicit D3D9 fixed-function view and projection transforms.

Instruction search for `IDirect3DDevice9::SetTransform` vtable calls (`+0xb0`) found only six call sites:

```text
0044d5c2  FUN_0044d2e0   SetTransform(..., 0x11, ...)  texture transform
0044d89e  FUN_0044d5d0   SetTransform(..., 0x11, ...)  texture transform reset/setup
0044df03  FUN_0044def0   SetTransform(..., 0x100, ...) D3DTS_WORLD
0044e548  FUN_0044e400   SetTransform(..., 2, ...)     D3DTS_VIEW
0044e667  FUN_0044e580   SetTransform(..., 3, ...)     D3DTS_PROJECTION
0044ef43  FUN_0044ee70   SetTransform(..., 0x10, ...)  texture transform
```

### View Transform

`FUN_0044e400 @ 0044e400` is the view transform setter. It copies the incoming 4x4 camera/source matrix into renderer state, builds a D3D view matrix through `FUN_004687d0`, then calls:

```c
(**(code **)(*(int *)*param_1 + 0xb0))((int *)*param_1, 2, local_40);
```

`2` is `D3DTS_VIEW`.

`FUN_004687d0` calls `FUN_004684e0`, which performs a rigid-transform inverse/transpose style conversion:

- transposes the 3x3 rotation basis,
- zeros the perspective/unused elements,
- computes translated camera terms via dot products,
- writes an identity final element.

This looks like a normal camera-world-to-view conversion, not a shader-only or pre-transformed camera path.

### Projection Transform

`FUN_0044e580 @ 0044e580` builds the projection matrix via `FUN_009676b4` and calls:

```c
(**(code **)(*(int *)*param_1 + 0xb0))((int *)*param_1, 3, param_1 + 0x1559);
```

`3` is `D3DTS_PROJECTION`.

Callers include:

```text
004216b5  FUN_00421530
00434a22  FUN_004342c0
00434ae6  FUN_004342c0
0044fba7  FUN_0044fae0
0044face  FUN_0044faa0
00450101  FUN_0044fd00
0045046a  FUN_0044fd00
008decfa  FUN_008dec80
```

### Render Loop Placement

`FUN_00421530` appears to be the main camera/render loop for a span of render entries. For each active entry, it:

1. applies viewport/target state,
2. calls `FUN_0044e580` to set projection,
3. conditionally calls `FUN_0044e400` to set view when the 4x4 matrix differs from the last submitted matrix,
4. dispatches render command lists through `FUN_004342c0`.

This means view/projection setup occurs before render command submission in at least this path.

### Shader Constant Check

Instruction search did not find obvious D3D9 vertex-shader / vertex-shader-constant calls at common vtable offsets searched (`+0x16c`, `+0x17c`, `+0x180`, `+0x184`) in the adapter area. This supports the conclusion that the relevant path is fixed-function transform state, not a shader-only camera path.

### RTX Remix Implication

The prior "should be compatible in principle" conclusion is stronger after this pass:

- world transform exists,
- view transform exists,
- projection transform exists,
- real indexed geometry exists,
- common shader-constant camera paths were not obvious in this adapter.

So the current RTX Remix failure is not explained by missing D3D9 camera APIs in the game. More plausible explanations are:

- Remix is missing the relevant view/projection state due to timing, pass changes, or render-target transitions;
- the game conditionally resubmits view only when the matrix changes, while Remix may need the camera resent before the pass it chooses to raytrace;
- a particular pass feeds a degenerate/non-rigid matrix into the view conversion, matching the runtime `Attempted invert a non-invertible matrix` warning;
- render-to-texture/composite behavior causes Remix to raytrace a pass that lacks the valid camera state from the main 3D pass.

Next RE target: verify the actual runtime values/order around `FUN_00421530 -> FUN_0044e580 -> FUN_0044e400 -> FUN_004342c0`, and consider a test patch/hook that forces `SetTransform(D3DTS_VIEW)` and `SetTransform(D3DTS_PROJECTION)` to be resent immediately before world draw command lists.
