<#
.SYNOPSIS
    Builds, stages and packages the migrated FpuProbe C# host plus its native
    FPU/ARM payload (M10 task 8.6 verification items 1 and 3).
.DESCRIPTION
    Pipeline, all outputs fresh under -OutDir (never inside shared build dirs):
      1. Configures and builds the ARM native payload DLL from repository
         sources with the pinned clang ARM32-UWP toolchain (--parallel 4),
         warning-free.
      2. Runs check-fpuprobe-expectations.ps1 (inventory + host arithmetic +
         ARM export inspection).
      3. Restores (nuget.org feed pin, Microsoft.NETCore.UniversalWindowsPlatform
         6.0.15) and builds the C# host ARM Release with the Community 2022
         MSBuild through .NET Native, warning-free.
      4. Stages FpuProbePayload.dll into the MSBuild AppX layout, packs with
         SDK 19041 makeappx, and signs with the CN=WebKitWebView development
         certificate unless -SkipSign.
    Never touches the phone or Device Portal; the DEVICE RUNBOOK in README.md
    carries the coordinator's install/launch commands.
.PARAMETER OutDir
    Fresh output root under %TEMP%\opencode.
.PARAMETER SdkVersion
    Windows SDK version; 10.0.19041.0 is the pinned ARM32 SDK.
.PARAMETER MsvcVersion
    Explicit MSVC toolset directory name; defaults to the newest installed.
.PARAMETER SignThumbprint
    Development certificate thumbprint (CN=WebKitWebView).
.PARAMETER SkipSign
    Build-only run: pack the appx layout without signing.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\m10-fpuprobe-86'),
    [string]$SdkVersion = '10.0.19041.0',
    [string]$MsvcVersion,
    [string]$SignThumbprint = '18FD65CF5E103CF96F04C2A451DC49C94788A40D',
    [switch]$SkipSign
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))
$probeDir = Join-Path $repo 'Tools\uwp\diagnostics\FpuProbe'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (Test-Path (Join-Path $OutDir 'package')) { throw 'Use a fresh -OutDir; an existing package may contain stale payloads' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

function Invoke-Logged([string]$label, [string]$exe, [string[]]$arguments, [string]$logFile) {
    Write-Host "--- $label ---"
    Write-Host "    $exe $($arguments -join ' ')"
    $output = & $exe @arguments 2>&1
    $output | ForEach-Object { "$_" } | Out-File -LiteralPath $logFile -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        $output | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" }
        throw "$label failed with exit code $LASTEXITCODE (log: $logFile)"
    }
    return ($output -join "`n")
}

function Assert-NoWarnings([string]$logText, [string]$label) {
    $hits = @($logText -split "`n" | Where-Object { $_ -match ': warning | warning (CS|MSB|APPX|XC)\d|WARNING:' })
    if ($hits.Count -gt 0) {
        $hits | Select-Object -First 10 | ForEach-Object { Write-Host "    $_" }
        throw "$label produced $($hits.Count) warning(s); expected warning-free"
    }
    Write-Host "    warning-free"
}

# 1. Native ARM payload DLL (read-only use of the shared env/toolchain files).
Write-Host 'Resolving the native ARM32 toolchain (shared discovery, not modified)...'
. (Join-Path $repo 'Tools\uwp\arm32-uwp-env.ps1') -SdkVersion $SdkVersion
$toolchain = (Resolve-Path (Join-Path $repo 'WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake')).Path
$payloadBuild = Join-Path $OutDir 'payload'
$payloadLog = Join-Path $OutDir 'payload-build.log'
$configLog = Invoke-Logged 'cmake configure (ARM payload)' 'cmake' @(
    '-S', $probeDir, '-B', $payloadBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_TOOLCHAIN_FILE=$toolchain") (Join-Path $OutDir 'payload-configure.log')
$buildLog = Invoke-Logged 'cmake build (ARM payload, --parallel 4)' 'cmake' @(
    '--build', $payloadBuild, '--parallel', '4') $payloadLog
# The pinned native toolchain passes its -B linker search dir on every compile
# line, which clang-cl reports as -Wunknown-argument. That driver note is
# toolchain-environmental (it fires for any target under this toolchain file),
# not payload code, so it is recorded here and excluded from the warning gate;
# any warning naming a payload source still fails below.
$payloadWarnings = ($configLog + "`n" + $buildLog) -split "`n" |
    Where-Object { $_ -notmatch 'unknown argument ignored in clang-cl' }
Assert-NoWarnings ($payloadWarnings -join "`n") 'ARM payload build'
$payloadDll = Get-ChildItem -LiteralPath $payloadBuild -Filter 'FpuProbePayload.dll' -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $payloadDll) { throw 'ARM payload DLL was not produced' }
Write-Host "    payload: $payloadDll"

# 2. Expectation proof (inventory + host arithmetic + ARM export inspection).
& (Join-Path $probeDir 'scripts\check-fpuprobe-expectations.ps1') -OutDir (Join-Path $OutDir 'check') -PayloadDll $payloadDll
if ($LASTEXITCODE -ne 0) { throw 'Expectation check failed' }

# 3. C# host ARM Release through .NET Native (Community 2022 MSBuild only).
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw "Community 2022 MSBuild missing: $msbuild" }
& $msbuild -version | ForEach-Object { Write-Host "    $_" }
$appOut = Join-Path $OutDir 'app\bin\'
$appObj = Join-Path $OutDir 'app\obj\'
$appPkg = Join-Path $OutDir 'app\packages\'
$csLog = Invoke-Logged 'MSBuild restore+build (C# ARM Release, /m:4)' $msbuild @(
    (Join-Path $probeDir 'host\FpuProbe.csproj'), '/restore', '/t:Build',
    '/p:Configuration=Release', '/p:Platform=ARM', '/m:4',
    "/p:OutputPath=$appOut", "/p:IntermediateOutputPath=$appObj", "/p:AppxPackageDir=$appPkg",
    "/p:MSBuildProjectExtensionsPath=$appObj", '/p:AppxBundle=Never', '/verbosity:minimal') (Join-Path $OutDir 'csharp-build.log')
Assert-NoWarnings $csLog 'C# ARM Release build'
# .NET Native proof: the Release|ARM configuration sets
# UseDotNetNativeToolchain=true, and the ilc intermediate plus the native
# image in the ilc layout below exist only when the toolchain ran.
$ilcExe = Join-Path $appOut 'ilc\WebKitFpuProbe.exe'
if (-not (Test-Path -LiteralPath $ilcExe)) { throw '.NET Native output missing: no ilc WebKitFpuProbe.exe' }
Write-Host '    .NET Native toolchain engaged (ilc WebKitFpuProbe.exe present)'

# 4. Stage payload into the MSBuild-verified closure, pack, sign. The packaged
# layout under .NET Native is bin\ilc\, but instead of re-deriving it, unpack
# MSBuild's own appx verbatim so the staged closure is exactly what MSBuild
# validated, plus the native payload DLL.
$msAppx = Get-ChildItem -LiteralPath $appPkg -Filter '*.appx' -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $msAppx) { throw "MSBuild produced no appx under $appPkg" }
Write-Host "    msbuild appx: $msAppx"
$sdkBin = "C:\Program Files (x86)\Windows Kits\10\bin\$SdkVersion\x64"
$stageDir = Join-Path $OutDir 'package'
$layoutDir = Join-Path $stageDir 'layout'
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
Invoke-Logged 'makeappx unpack (MSBuild closure)' (Join-Path $sdkBin 'makeappx.exe') @(
    'unpack', '/p', $msAppx, '/d', $layoutDir, '/o') (Join-Path $OutDir 'unpack.log')
foreach ($need in @('AppxManifest.xml', 'WebKitFpuProbe.exe', 'resources.pri')) {
    if (-not (Test-Path -LiteralPath (Join-Path $layoutDir $need))) { throw "Staged closure missing $need" }
}
foreach ($logo in @('Assets\Logo50.png', 'Assets\Logo150.png', 'Assets\Logo44.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path $layoutDir $logo))) { throw "Staged closure missing $logo" }
    Write-Host "    logo: $logo"
}
Copy-Item -LiteralPath $payloadDll -Destination (Join-Path $layoutDir 'FpuProbePayload.dll') -Force
Write-Host '    staged: FpuProbePayload.dll'
# Drop the unpacked footprint/signature so the repack cannot reuse stale ones.
foreach ($stale in @('AppxBlockMap.xml', 'AppxSignature.p7x')) {
    $stalePath = Join-Path $layoutDir $stale
    if (Test-Path -LiteralPath $stalePath) { Remove-Item -LiteralPath $stalePath -Force }
}
$appx = Join-Path $stageDir 'FpuProbe_1.0.0.12_ARM.appx'
Invoke-Logged 'makeappx pack' (Join-Path $sdkBin 'makeappx.exe') @(
    'pack', '/d', $layoutDir, '/p', $appx, '/o') (Join-Path $OutDir 'pack.log')
Write-Host "    packed: $appx"
if (-not $SkipSign) {
    Invoke-Logged 'signtool sign' (Join-Path $sdkBin 'signtool.exe') @(
        'sign', '/fd', 'SHA256', '/sha1', $SignThumbprint, $appx) (Join-Path $OutDir 'sign.log')
    Write-Host "    signed with $SignThumbprint"
    Invoke-Logged 'signtool verify' (Join-Path $sdkBin 'signtool.exe') @(
        'verify', '/pa', $appx) (Join-Path $OutDir 'verify.log')
    Write-Host '    signature verifies'
}
else {
    Write-Host '    -SkipSign: appx layout is packed but unsigned'
}

Write-Host ''
Write-Host 'FpuProbe C# migration build passed.'
Write-Host "  payload : $payloadDll"
Write-Host "  package : $appx"
Write-Host '  identity: WebKitWebView.FpuProbe_1.0.0.12_arm (EntryPoint WebKitFpuProbe.App)'
exit 0
