# Rebuilds Tools/UWP/lib/arm-crtstart.lib from toolchain/ sources.
# Run from the WebKit repo root in PowerShell. Requires VS2022 BuildTools
# with clang-cl (see openspec change wkview-m0-toolchain).
# NOTE: clang_rt.builtins-arm.lib is NOT rebuilt here (needs an llvm-project
# sparse checkout; recipe in Tools/UWP/ARM-WALLS.md). It is tracked in lib/.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$clang = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe"
$lib = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\llvm-lib.exe"
$inc = @(
    "-DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP",
    "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um",
    "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared",
    "-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt"
)
$objs = @()
foreach ($f in @("crmain", "crwmain", "crwinmain", "crwwinmain", "crrtti", "crdllmain")) {
    $o = Join-Path $root "lib\$f.obj"
    & $clang --target=thumbv7-unknown-windows-msvc -c (Join-Path $root "toolchain\$f.c") -Fo"$o" @inc /MD /GR- /EHs-c-
    $objs += $o
}
& $clang --target=thumbv7-unknown-windows-msvc -c (Join-Path $root "toolchain\crrt.cpp") -Fo (Join-Path $root "lib\crrt.obj") @inc /MD /GR- /EHs-c-
$objs += Join-Path $root "lib\crrt.obj"
& "C:\Users\Longi\scoop\apps\llvm\current\bin\clang.exe" --target=thumbv7-unknown-windows-msvc -c (Join-Path $root "toolchain\crtvft.S") -o (Join-Path $root "lib\crtvft.obj")
$objs += Join-Path $root "lib\crtvft.obj"
Remove-Item (Join-Path $root "lib\arm-crtstart.lib") -Force -ErrorAction SilentlyContinue
& $lib /OUT:(Join-Path $root "lib\arm-crtstart.lib") @objs
Remove-Item (Join-Path $root "lib\*.obj") -Force
Write-Output "arm-crtstart.lib rebuilt"
