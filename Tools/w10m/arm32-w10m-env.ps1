<#
.SYNOPSIS
Discovers the native ARM32 UWP compiler/SDK and exports the CMake flag inputs.
.DESCRIPTION
Dot-source before native CMake configure. Paths come from the checkout and
installation discovery or explicit overrides; space-free flag paths are resolved
through GetShortPathName rather than assuming another machine's 8.3 aliases.
#>
[CmdletBinding()]
param(
    [string]$VsPath,
    [string]$MsvcVersion,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.19041.0',
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    foreach ($instance in (& $vswhere -all -products '*' -format json | ConvertFrom-Json)) {
        if ((Test-Path (Join-Path $instance.installationPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe')) -and
            (Get-ChildItem (Join-Path $instance.installationPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
                Where-Object { Test-Path (Join-Path $_.FullName 'lib\arm\uwp\vcruntime.lib') })) {
            $VsPath = $instance.installationPath
            break
        }
    }
}
if (-not $VsPath) { throw 'No native ARM-capable VS installation; supply -VsPath' }
if (-not $MsvcVersion) {
    $MsvcVersion = (Get-ChildItem (Join-Path $VsPath 'VC\Tools\MSVC') -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'lib\arm\uwp\vcruntime.lib') } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
if (-not $SdkRoot) { $SdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
$toolset = Join-Path $VsPath "VC\Tools\MSVC\$MsvcVersion"
$sdkInclude = Join-Path $SdkRoot "Include\$SdkVersion"
$sdkLib = Join-Path $SdkRoot "Lib\$SdkVersion"
$clang = Join-Path $VsPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
$llvmBin = Split-Path -Parent $clang
$rc = Join-Path $SdkRoot "bin\$SdkVersion\x64\rc.exe"
foreach ($required in @($clang, $rc, "$llvmBin\lld-link.exe", "$llvmBin\llvm-lib.exe",
        "$toolset\include\intrin.h", "$toolset\lib\arm\uwp\vcruntime.lib", "$toolset\lib\arm\uwp\msvcprt.lib",
        "$sdkInclude\ucrt", "$sdkInclude\um", "$sdkInclude\shared", "$sdkInclude\winrt",
        "$sdkLib\um\arm\OneCoreUAP.lib", "$sdkLib\ucrt\arm\ucrt.lib",
        "$repo\Tools\w10m\lib\arm-crtstart.lib", "$repo\Tools\w10m\lib\clang_rt.builtins-arm.lib")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required native ARM32 input missing: $required" }
}
if (-not ('NativeBuildPathUWP' -as [type])) {
    Add-Type -TypeDefinition 'using System.Runtime.InteropServices; using System.Text; public static class NativeBuildPathUWP { [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern uint GetShortPathName(string path, StringBuilder output, uint size); }'
}

# vcpkg/meson split native flag strings, so every embedded path must be space-free.
function Get-NativeFlagPathUWP([string]$path) {
    $full = (Resolve-Path -LiteralPath $path).Path
    $buffer = New-Object Text.StringBuilder 32768
    $length = [NativeBuildPathUWP]::GetShortPathName($full, $buffer, $buffer.Capacity)
    if (-not $length -or $length -ge $buffer.Capacity) { throw "Cannot resolve native flag path: $full" }
    $short = $buffer.ToString().Replace('\', '/')
    if ($short -match '\s|[^\x00-\x7F]') { throw "Native flags require a space-free ASCII path; move/override input: $full" }
    return $short
}
$include = Get-NativeFlagPathUWP "$toolset\include"
$headers = @('ucrt','um','shared','winrt') | ForEach-Object { '-imsvc' + (Get-NativeFlagPathUWP "$sdkInclude\$_") }
$nativeLib = Get-NativeFlagPathUWP "$toolset\lib\arm\uwp"
$umLib = Get-NativeFlagPathUWP "$sdkLib\um\arm"
$ucrtLib = Get-NativeFlagPathUWP "$sdkLib\ucrt\arm"
$support = Get-NativeFlagPathUWP "$repo\Tools\w10m\lib"
$builtins = Get-NativeFlagPathUWP "$repo\Tools\w10m\lib\clang_rt.builtins-arm.lib"
$startup = Get-NativeFlagPathUWP "$repo\Tools\w10m\lib\arm-crtstart.lib"
$compat = Get-NativeFlagPathUWP "$repo\Tools\w10m\toolchain\w10m-desktop-apis.h"
$linkerBin = Get-NativeFlagPathUWP $llvmBin
$env:WK_W10M_CLANG_CL = $clang.Replace('\', '/')
$env:WK_W10M_RC = Get-NativeFlagPathUWP $rc
$env:WK_W10M_BUILTINS_ARM = $builtins
$env:WK_W10M_NATIVE_C_FLAGS = "--target=armv7-unknown-windows-msvc -B$linkerBin -fuse-ld=lld /MD -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP -D_WIN32_WINNT=0x0A00 -D_HAS_EXCEPTIONS=0 -Wno-error=incompatible-function-pointer-types -DU_PLATFORM_HAS_WINUWP_API=1 -DSQLITE_OMIT_SEH -D_WINRT_DLL /FI$include/intrin.h -FI$compat /EHs-c- /GR- -imsvc$include $($headers -join ' ')"
$env:WK_W10M_NATIVE_LINK_FLAGS = "/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:CONSOLE /FORCE:MULTIPLE /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd /NODEFAULTLIB:msvcrt /NODEFAULTLIB:msvcrtd /NODEFAULTLIB:msvcprt /NODEFAULTLIB:msvcprtd /NODEFAULTLIB:oldnames /LIBPATH:$umLib /LIBPATH:$ucrtLib /LIBPATH:$nativeLib /LIBPATH:$support $builtins $startup $nativeLib/vcruntime.lib $nativeLib/msvcprt.lib $ucrtLib/ucrt.lib $umLib/OneCoreUAP.lib"
$env:WK_W10M_VS_ROOT = $VsPath
$env:WK_W10M_TOOLSET_ROOT = $toolset
$env:WK_W10M_SDK_ROOT = $SdkRoot
$env:WK_W10M_SDK_VERSION = $SdkVersion
$env:INCLUDE = "$toolset\include;$sdkInclude\ucrt;$sdkInclude\um;$sdkInclude\shared;$sdkInclude\winrt"
$env:LIB = "$sdkLib\um\arm;$sdkLib\ucrt\arm"
$env:Path = "$llvmBin;" + $env:Path
if ($VcpkgRoot) {
    if (-not (Test-Path -LiteralPath $VcpkgRoot)) { throw "vcpkg override missing: $VcpkgRoot" }
    $env:VCPKG_ROOT = $VcpkgRoot
    $gperf = Join-Path $VcpkgRoot 'installed\x64-windows\tools\gperf'
    if (Test-Path $gperf) { $env:Path = "$gperf;" + $env:Path }
}
"Native ARM32 UWP environment: MSVC $MsvcVersion, SDK $SdkVersion, clang-cl $clang"
