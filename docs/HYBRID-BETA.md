# PSVR2 Passthrough v0.7.0-hybrid-beta.1

This experimental build displays PSVR2Toolkit's maximum-undistorted camera
stream through an OpenXR composition layer. Keyboard, XInput and HOTAS
activation and opacity remain configurable. SteamVR Room View is not toggled.

**Status:** experimental test candidate; Windows and headset execution remain unverified.

## Requirements

- Windows x64, PSVR2 PC adapter, SteamVR as the OpenXR runtime.
- PSVR2Toolkit experimental 2 or later, with SteamVR camera access enabled.
- An OpenXR game using D3D11 or D3D12. D3D12 uses D3D11-on-12 interoperability.

Native OpenVR games remain outside this layer's scope.

## Install and compare

1. Exit your game. Extract this beta into its own folder; retain the old build.
2. Run `install.bat`. It replaces the previous layer registration so only one
   passthrough build is enabled, then opens the configuration app.
3. Confirm the binding and hold/toggle mode. Use opacity 1.0 for the first visual comparison, save, then start SteamVR and your game.
4. Activate passthrough. Initial camera startup can take a moment.
5. Check the configurator's last runtime report or
   `%LOCALAPPDATA%\PSVR2PassthroughLayer\layer.log` if no image appears.

For rollback, exit the game and run the old build's installer. Beta settings
are stored in `config-hybrid-beta.json`; the old `config.json` is left intact.
On first load, the beta imports only enabled/always-visible, opacity, binding
and hold/toggle settings from the old file. Saving writes only the beta file.

Removed settings: manual toe-out/tilt/roll and linked eyes, camera separation,
IPD angular correction, zoom, brightness, contrast, sharpening, lens-correction
toggle and estimated-latency/reprojection controls. These are not applied by
the new renderer. No binding means hidden unless Always visible is enabled.

## Pipeline

Toolkit ingests and converts the camera images. SteamVR exposes its
maximum-undistorted stereo texture and matching exposure timestamp. The layer
reads the published physical camera transforms and projection matrices,
converts exposure QPC ticks to OpenXR time, locates the head in the game's
reference space at that time, and submits the two camera views there.

The layer uses the shared D3D11 texture, a simple stereo split/opacity pass and
GPU copies into OpenXR swapchains. It adds no CPU video readback, sharpening,
tone curve or second lens warp. Toolkit's own upstream NV12 conversion and
readback still exist. Our shader converts gamma-encoded UNORM camera colours
to linear values before alpha premultiplication; an sRGB SRV already performs
that decode. The output matches the negotiated OpenXR swapchain format.

Camera access, projection, physical transforms and exposure time must validate.
An unsupported stream, missing conversion extension, unavailable exposure pose
or frame older than 250 ms hides passthrough. The layer does not silently switch
to old camera processing or guessed latency. It retries camera connection
errors every two seconds while passthrough is requested. There is no wait for
a future camera frame. Once acquired, the camera service stays active until
the game session ends, keeping hotkey activation responsive.

The layer uses exposure-time head poses and camera origins. It does not
reconstruct scene depth or promise exact camera-to-eye translation correction
for objects at all distances. SteamVR Room View's additional presentation
processing remains unknown; this is an experiment, not a Room View clone.

## Verification and headset test

On the headset compare small stationary text, dark detail, stereo alignment
at near/far distances, and head turns against the old build and Room View.
Check recentering, camera startup, hold/toggle bindings, opacity, game frame
times, DCS quad views and a D3D12 title. Desktop+ overlay ordering can be tested
later. Native Room View activation/coexistence has not been verified here.

## Build

With Visual Studio 2022, CMake and bootstrapped vcpkg installed:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
.\scripts\build-beta.ps1
```

The script builds x64 Release with static dependencies, runs the portable math
and config tests, and collects a package under `build\hybrid-beta\package`.
It does not install the layer or publish a release.
