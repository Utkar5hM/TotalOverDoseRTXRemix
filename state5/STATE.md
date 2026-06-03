# State 5 - SkyBox material texture classification

Saved: 2026-06-03

Summary:
- ASI build with TOD SkyBox::Render scoped material-command capture.
- Confirmed 10 TOD SkyBox material hashes from runtime capture.
- Config uses rtx.skyBoxTextures and disables rtx.skyAutoDetect heuristics.
- Root and scripts rtx.conf are byte-identical.

Active ASI:
- scripts/TODCameraResend.asi size: 137216 bytes
- scripts/TODCameraResend.asi timestamp: 2026-06-03 19:31:13

Config SHA256:
- root/rtx.conf: 9C75DA1B60A8E1BF686347CC11D241616AC219E690BCD1FE46A59CE7941F19EC
- scripts/rtx.conf: 9C75DA1B60A8E1BF686347CC11D241616AC219E690BCD1FE46A59CE7941F19EC

Key sky config:
- rtx.skyAutoDetect = 0
- rtx.skyBoxTextures = 0x3848EB42A9454B29, 0x38F01A3A44A105E4, 0x3B3C8BFC46BDDD15, 0x3CBFF5A558504B9F, 0x988E3EC036BC1681, 0x98BC8203A806B55D, 0xBFD77BC11E21AE42, 0xDDF1BE54CD1936C8, 0xDF12A2DCB829B60D, 0xE4F08883065FBB37

Evidence:
- re_docs/rtx_remix_runtime_debug_log.md
- re_docs/tod_render_distance_texture_handling_re_20260603.md
- re_docs/skybox_material_contact_sheet_20260603.png
