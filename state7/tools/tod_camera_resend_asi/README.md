# TODCameraResend ASI

This is a diagnostic RTX Remix hook for Total Overdose.

It hooks the D3D9 fixed-function camera path and re-submits the latest
non-identity `D3DTS_VIEW` plus latest `D3DTS_PROJECTION` immediately before
fixed-function indexed draw calls.

The goal is to test whether Remix's `not detecting a valid camera` runtime log is
caused by the game changing render state between camera setup and world draws.

It also logs whether draw calls are using pre-transformed `XYZRHW` / `POSITIONT`
vertices or vertex shaders, since those paths do not use fixed-function camera
transforms in the way RTX Remix expects.

Build:

```bat
build.bat
```

Output:

```text
scripts\TODCameraResend.asi
```

Runtime log:

```text
rtx-remix\logs\tod-camera-resend.log
```

To disable the test, remove or rename `scripts\TODCameraResend.asi`.
