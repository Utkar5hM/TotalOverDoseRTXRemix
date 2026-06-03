# State 4 - 2026-06-02 23:48 IST

Current working Total Overdose RTX Remix support state.

Observed working:
- RTX active.
- HUD visible.
- Sparks/blood visible.
- Previously missing breakable/corrugated panel texture visible.
- Opaque managed A8 world-copy category removed after sky/world material corruption; sky may be black and is deferred for a proper Remix sky classification pass.

Known remaining work:
- Proper sky/backdrop classification.
- Render-distance / capture framing / missing far-screen geometry investigation in TOD.exe.
- Analyze TOD material and texture submission paths instead of adding broad texture-copy rules.

Important active files:
- scripts/TODCameraResend.asi
- scripts/rtx.conf (active Remix config path observed in logs)
- root/rtx.conf (fallback/mirrored root config may differ if Remix UI saved scripts/rtx.conf)
- tools/tod_camera_resend_asi/TODCameraResend.cpp
- re_docs/*.md
