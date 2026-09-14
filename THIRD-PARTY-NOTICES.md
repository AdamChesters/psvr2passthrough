# Third-party notices

The OpenVR SDK header and runtime are from ValveSoftware/openvr commit
`0924064316de3effbcd1acf1e309182a2deb1c05`. Its license is included in
`third_party/openvr/LICENSE`, or `LICENSE-OpenVR.txt` in a package.

PSVR2Toolkit by Bnuuy Solutions provides the camera ingestion, conversion,
calibration and distortion processing used by this beta. Install it separately.
Reference release: `v1.0.0-experimental-2`, commit
`b89b78a8e9387bbe07f02649cefb7dcdeea76618`.
This beta consumes its output via SteamVR; it does not include Toolkit's driver.
https://github.com/BnuuySolutions/PSVR2Toolkit

The configuration UI uses Dear ImGui (MIT), logging uses spdlog (MIT) and fmt
(MIT), JSON serialization uses nlohmann/json (MIT), and OpenXR declarations
come from the Khronos OpenXR SDK (Apache-2.0 or MIT). When distributing a
build, include the license files for the dependency versions used to build it.
