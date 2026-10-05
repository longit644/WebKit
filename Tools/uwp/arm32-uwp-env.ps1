# WebKitWebView ARM32-UWP env (v0.2). Run in PowerShell before CMake configure.
# ASCII paths only. VS2022 BuildTools + Windows 10 SDK 10.0.19041.0, the newest
# installed SDK that still ships the ARM32 (um/arm, ucrt/arm) link libraries.

$VSBT = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$MSVC = "$VSBT\VC\Tools\MSVC\14.44.35207"
$SDK = "C:\Program Files (x86)\Windows Kits\10"
$SDKVER = "10.0.19041.0"

$env:VCPKG_ROOT = "C:\vcpkg"

$env:Path = "$VSBT\VC\Tools\Llvm\bin;C:\Users\Longi\scoop\apps\ruby\current\bin;C:\Strawberry\perl\bin;C:\ProgramData\chocolatey\bin;C:\vcpkg\installed\x64-windows\tools\gperf;" + $env:Path

$INC_SDK = "$SDK\Include\$SDKVER"
$LIB_SDK = "$SDK\Lib\$SDKVER"

$env:INCLUDE = "$MSVC\include;$INC_SDK\ucrt;$INC_SDK\um;$INC_SDK\shared;$INC_SDK\winrt"
$env:LIB = "$LIB_SDK\um\arm;$LIB_SDK\ucrt\arm"

Write-Output "WK-UWP-ARM env ready: $(clang-cl --version | Select-Object -First 1)"
