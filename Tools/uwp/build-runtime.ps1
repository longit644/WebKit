# Rebuilds Tools/uwp/lib/arm-crtstart.lib from toolchain/ sources.
# Run from anywhere: pwsh -File <repo>/Tools/uwp/build-runtime.ps1
# Requires VS2022 BuildTools with clang-cl (see openspec wkview-m0-toolchain).
# NOTE: clang_rt.builtins-arm.lib is NOT rebuilt here (needs an llvm-project
# sparse checkout; recipe in Tools/UWP/ARM-WALLS.md). It is tracked in lib/.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$clang = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe"
$llvm = "C:\Users\Longi\scoop\apps\llvm\current\bin\clang.exe"
$lib = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\llvm-lib.exe"
$incDir = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0"
$libOut = Join-Path $root "lib\arm-crtstart.lib"
$objs = @()
foreach ($f in @("crmain", "crwmain", "crwinmain", "crwwinmain", "crrtti", "crdllmain")) {
    $src = Join-Path $root "toolchain\$f.c"
    $o = Join-Path $root "lib\$f.obj"
    & $clang --target=armv7-unknown-windows-msvc -c "$src" -Fo"$o" "-DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP" "-I$incDir\um" "-I$incDir\shared" "-I$incDir\ucrt" /MD /GR- /EHs-c-
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $f" }
    $objs += $o
}
$crrtObj = Join-Path $root "lib\crrt.obj"
& $clang --target=armv7-unknown-windows-msvc -c (Join-Path $root "toolchain\crrt.cpp") -Fo"$crrtObj" "-DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP" "-I$incDir\um" "-I$incDir\shared" "-I$incDir\ucrt" /MD /GR- /EHs-c-
if ($LASTEXITCODE -ne 0) { throw "compile failed: crrt" }
$objs += $crrtObj
$crtvftObj = Join-Path $root "lib\crtvft.obj"
& $llvm --target=armv7-unknown-windows-msvc -c (Join-Path $root "toolchain\crtvft.S") -o "$crtvftObj"
if ($LASTEXITCODE -ne 0) { throw "compile failed: crtvft" }
$objs += $crtvftObj
foreach ($o in $objs) { if (-not (Test-Path -LiteralPath $o)) { throw "missing object: $o" } }
Remove-Item -LiteralPath $libOut -Force -ErrorAction SilentlyContinue
& $lib "/OUT:$libOut" @objs
if ($LASTEXITCODE -ne 0) { throw "lib failed" }
Remove-Item (Join-Path $root "lib\*.obj") -Force
# Empty m.lib: satisfies Unix -lm (math lives in ucrt on Windows).
& $lib "/OUT:$(Join-Path $root 'lib\m.lib')" /llvmlibempty
Write-Output "arm-crtstart.lib rebuilt"
