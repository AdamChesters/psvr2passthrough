# PSVR2 Passthrough Layer: Toolkit hybrid beta

This branch contains `0.7.0-hybrid-beta.1`, an experimental OpenXR layer using
PSVR2Toolkit's SteamVR camera stream with our opacity and keyboard/gamepad/HOTAS
controls. It replaces the old direct-camera processing and manual geometry UI.

**Status, 14 September 2026:** implemented and cross-compiled for Windows x64.
Math/config tests passed. Windows runtime and headset behavior are unverified;
no visual-quality or latency improvement has been measured. Testing is deferred
for a few days at the owner's request. No GitHub release has been created for
this beta.

- the installation and test guidance below: decisions, exact build, remaining checks.
- [Beta guide](docs/HYBRID-BETA.md): requirements, pipeline, install and rollback.
- the installation and test guidance below: passed and unrun checks.
- the installation and test guidance below: upstream baseline.
- [Windows test package](dist/PSVR2PassthroughLayer-v0.7.0-hybrid-beta.1.zip).

Requires Windows x64, PSVR2 with PC adapter, SteamVR as the OpenXR runtime,
PSVR2Toolkit experimental 2 or later, and a D3D11 or D3D12 OpenXR application.
This layer does not toggle native Room View. Native OpenVR applications are
outside its scope.

For a native build with Visual Studio 2022, CMake and bootstrapped vcpkg:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
.\scripts\build-beta.ps1
```

The checked-in ZIP is the earlier MinGW test build, including its original
source snapshot and documentation. Current branch documentation supersedes
that snapshot. It is an unverified test candidate, not a released version.

Previous release instructions, screenshots, compatibility claims and
attributions are preserved in [legacy documentation](docs/LEGACY-README.md).
They do not establish compatibility for this beta. The current third-party
credits are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). License: MIT.
