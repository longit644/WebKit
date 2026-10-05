<#
.SYNOPSIS
    Builds the ARM32 packaged XAML Application host for the probe lifetime gate.

.DESCRIPTION
    The console harness verifies rejection and HRESULT translation on the desktop.
    This host supplies the package/UI-thread context required to construct the
    UserControl, while task 2.5 adds the compiled-XAML/type-provider surface.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-probe-arm32-host'),
    [string]$MsvcVersion = '14.44.35207',
    [string]$SdkVersion = '10.0.19041.0',
    [string]$SignThumbprint = '18FD65CF5E103CF96F04C2A451DC49C94788A40D',
    [switch]$SkipSign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$kitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$toolset = Join-Path $vsRoot "VC\Tools\MSVC\$MsvcVersion"
$sdkInclude = Join-Path $kitsRoot "Include\$SdkVersion"
$sdkLib = Join-Path $kitsRoot "Lib\$SdkVersion"
$sdkBin = Join-Path $kitsRoot "bin\$SdkVersion\x64"
$cl = Join-Path $toolset 'bin\Hostx64\arm\cl.exe'
$link = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\lld-link.exe'
$componentScript = Join-Path $PSScriptRoot 'build-arm32-cx-probe.ps1'
$testsSource = Join-Path $PSScriptRoot 'XamlProbeLifetimeTests.cpp'
$hostSource = Join-Path $PSScriptRoot 'XamlProbeLifetimeCoreHost.cpp'
$manifest = Join-Path $PSScriptRoot 'XamlProbeLifetimeHost.arm.appxmanifest'
$armCrtStart = Join-Path $repoRoot 'Tools\uwp\lib\arm-crtstart.lib'

foreach ($required in @(
        $toolset, $sdkInclude, $sdkLib, $cl, $link, $componentScript,
        $testsSource, $hostSource, $manifest, $armCrtStart)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required host build input not found: $required"
    }
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$metadataDir = Join-Path $OutDir 'metadata'
New-Item -ItemType Directory -Path $metadataDir -Force | Out-Null

Write-Host 'ARM32 C++/CX XAML probe lifetime host'
Write-Host "  msvc toolset : $toolset"
Write-Host "  sdk          : $SdkVersion"
Write-Host "  output       : $OutDir"
Write-Host ''

# Build the component and WinMD with the canonical ARM32 recipe first. Keeping
# this as a child invocation prevents the host recipe from drifting from the
# component recipe that already carries the ABI/export checks.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $componentScript `
    -OutDir $OutDir -MsvcVersion $MsvcVersion -SdkVersion $SdkVersion
if ($LASTEXITCODE -ne 0) {
    throw "ARM32 component build failed with exit code $LASTEXITCODE"
}

$include = @(
    (Join-Path $toolset 'include'),
    (Join-Path $sdkInclude 'ucrt'),
    (Join-Path $sdkInclude 'um'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'winrt')
) -join ';'
$previousInclude = [Environment]::GetEnvironmentVariable('INCLUDE', 'Process')
[Environment]::SetEnvironmentVariable('INCLUDE', $include, 'Process')
try {
    & $cl /nologo /c /EHsc /MD /DXAML_PROBE_LIFETIME_LIBRARY `
        ("/I" + $metadataDir) ("/Fo" + (Join-Path $OutDir 'XamlProbeLifetimeTests.arm.obj')) $testsSource
    if ($LASTEXITCODE -ne 0) {
        throw "ARM32 lifetime test object failed with exit code $LASTEXITCODE"
    }

    & $cl /nologo /c /EHsc /MD `
        ("/I" + $metadataDir) `
        ("/Fo" + (Join-Path $OutDir 'XamlProbeLifetimeCoreHost.arm.obj')) $hostSource
    if ($LASTEXITCODE -ne 0) {
        throw "ARM32 XAML host object failed with exit code $LASTEXITCODE"
    }
}
finally {
    [Environment]::SetEnvironmentVariable('INCLUDE', $previousInclude, 'Process')
}

$hostExe = Join-Path $OutDir 'XamlProbeLifetimeCoreHost.exe'
$armLib = Join-Path $toolset 'lib\arm\store'
$umArm = Join-Path $sdkLib 'um\arm'
$ucrtArm = Join-Path $sdkLib 'ucrt\arm'
& $link /nologo /machine:arm /appcontainer /force:multiple `
    ("/out:$hostExe") ("/libpath:$armLib") ("/libpath:$umArm") ("/libpath:$ucrtArm") `
    ("/libpath:" + (Split-Path -Parent $armCrtStart)) `
    (Join-Path $OutDir 'XamlProbeLifetimeTests.arm.obj') `
    (Join-Path $OutDir 'XamlProbeLifetimeCoreHost.arm.obj') `
    vccorlib.lib msvcprt.lib msvcrt.lib vcruntime.lib ucrt.lib OneCoreUAP.lib `
    runtimeobject.lib ole32.lib kernel32.lib arm-crtstart.lib `
    /subsystem:windows /entry:WKProbeLifetimeStartup
if ($LASTEXITCODE -ne 0) {
    throw "ARM32 XAML host link failed with exit code $LASTEXITCODE"
}

$packageDir = Join-Path $OutDir 'package'
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageDir 'Assets') -Force | Out-Null
Copy-Item -LiteralPath $hostExe -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $OutDir 'XamlProbeComponent.dll') -Destination $packageDir -Force
Copy-Item -LiteralPath (Join-Path $metadataDir 'XamlProbe.winmd') -Destination $packageDir -Force
Copy-Item -LiteralPath $manifest -Destination (Join-Path $packageDir 'AppxManifest.xml') -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'Websites\webkit.org\favicon.png') `
    -Destination (Join-Path $packageDir 'Assets\probe.png') -Force

$package = Join-Path $OutDir 'XamlProbeLifetimeHost-arm.appx'
$makeappx = Join-Path $sdkBin 'makeappx.exe'
if (-not (Test-Path -LiteralPath $makeappx)) {
    throw "makeappx.exe not found: $makeappx"
}
& $makeappx pack /d $packageDir /p $package /o
if ($LASTEXITCODE -ne 0) {
    throw "makeappx failed with exit code $LASTEXITCODE"
}

if (-not $SkipSign) {
    $signtool = Join-Path $sdkBin 'signtool.exe'
    if (-not (Test-Path -LiteralPath $signtool)) {
        throw "signtool.exe not found: $signtool"
    }
    & $signtool sign /fd SHA256 /sha1 $SignThumbprint $package
    if ($LASTEXITCODE -ne 0) {
        throw "package signing failed with exit code $LASTEXITCODE"
    }
}

Write-Host ''
Write-Host "ARM32 host executable: $hostExe"
Write-Host "ARM32 package:        $package"
