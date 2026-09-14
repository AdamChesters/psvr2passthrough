param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $VcpkgRoot) { throw "Set VCPKG_ROOT to your bootstrapped vcpkg directory." }
$build = Join-Path $root "build\hybrid-beta"
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $build "package" }
function Checked { param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Checked cmake @("-S", $root, "-B", $build, "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake", "-DVCPKG_TARGET_TRIPLET=x64-windows-static")
Checked cmake @("--build", $build, "--config", "Release", "--parallel")
$tests = Join-Path $build "tests"
Checked cmake @("-S", "$root/tests", "-B", $tests, "-A", "x64",
    "-DPSVR2_JSON_INCLUDE_DIR=$build/vcpkg_installed/x64-windows-static/include")
Checked cmake @("--build", $tests, "--config", "Release")
Checked ctest @("--test-dir", $tests, "-C", "Release", "--output-on-failure")
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Copy-Item "$build/src/layer/Release/PSVR2PassthroughLayer.dll" $OutputDirectory
Copy-Item "$build/src/layer/Release/openvr_api.dll" $OutputDirectory
Copy-Item "$build/src/gui/Release/PSVR2PassthroughConfig.exe" $OutputDirectory
# The release manifest must use a relative DLL path, never the build machine's path.
Copy-Item "$root/cmake/PSVR2PassthroughLayer.json.in" "$OutputDirectory/PSVR2PassthroughLayer-hybrid-beta.json"
Copy-Item "$root/scripts/install.bat", "$root/scripts/install_layer.ps1", "$root/scripts/uninstall_layer.ps1" $OutputDirectory
Copy-Item "$root/docs/HYBRID-BETA.md" "$OutputDirectory/README.md"
Copy-Item "$root/LICENSE" $OutputDirectory
Copy-Item "$root/third_party/openvr/LICENSE" "$OutputDirectory/LICENSE-OpenVR.txt"
Copy-Item "$root/THIRD-PARTY-NOTICES.md" $OutputDirectory
$licenses = Join-Path $OutputDirectory "licenses"
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Get-ChildItem "$build/vcpkg_installed/x64-windows-static/share/*/copyright" | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $licenses ($_.Directory.Name + ".txt"))
}
Write-Host "Beta package: $OutputDirectory"
