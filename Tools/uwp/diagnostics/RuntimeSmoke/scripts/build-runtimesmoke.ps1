<#
.SYNOPSIS
    Builds the migrated RuntimeSmoke payload (ARM native) and C# host
    (ARM Release with .NET Native) into fresh directories under %TEMP%.

.DESCRIPTION
    M10 task 8.5 build driver. Never builds inside shared build trees; their
    outputs are only referenced read-only by the stage script. Fails when any
    compiler warning appears. Uses --parallel 4 for the native build and /m:4
    for MSBuild because three sibling agents build concurrently.

.PARAMETER PayloadDir
    Fresh native build directory (defaults under %TEMP%\opencode).

.PARAMETER HostDir
    Fresh C# output directory (defaults under %TEMP%\opencode).

.PARAMETER HostObjDir
    Fresh C# intermediate directory (defaults under %TEMP%\opencode).

.PARAMETER AppxDir
    Fresh MSBuild package directory (defaults under %TEMP%\opencode).
    Without this override MSBuild would create AppPackages inside the source
    tree; the stage script consumes the test appx from here.
#>
[CmdletBinding()]
param(
    [string]$PayloadDir = (Join-Path $env:TEMP 'opencode\m10-85-payload'),
    [string]$HostDir = (Join-Path $env:TEMP 'opencode\m10-85-host'),
    [string]$HostObjDir = (Join-Path $env:TEMP 'opencode\m10-85-host-obj'),
    [string]$AppxDir = (Join-Path $env:TEMP 'opencode\m10-85-apppackages')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This script lives in Tools/uwp/diagnostics/RuntimeSmoke/scripts, so the
# repository root is five levels up; no developer-home path is embedded.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))
$script:AppRoot = Join-Path $PSScriptRoot '..'
$script:ToolchainFile = Join-Path $script:RepoRoot 'WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake'
$script:MsBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'

if (-not (Test-Path -LiteralPath $script:MsBuild)) {
    throw "Community 2022 MSBuild not found: $script:MsBuild"
}
if (-not (Test-Path -LiteralPath $script:ToolchainFile)) {
    throw "ARM32 UWP toolchain file not found: $script:ToolchainFile"
}

foreach ($directory in @($PayloadDir, $HostDir, $HostObjDir, $AppxDir)) {
    if (Test-Path -LiteralPath $directory) {
        throw "Use a fresh output directory for each build: $directory already exists"
    }
}

Write-Host '--- native payload (ARM, clang) ---'
. (Join-Path $script:RepoRoot 'Tools\uwp\arm32-uwp-env.ps1')
& cmake -S $script:AppRoot -B $PayloadDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$script:ToolchainFile" 2>&1 |
    Tee-Object -FilePath (Join-Path $PayloadDir 'configure.log')
if ($LASTEXITCODE -ne 0) { throw "Payload configure failed" }
& cmake --build $PayloadDir --parallel 4 2>&1 |
    Tee-Object -FilePath (Join-Path $PayloadDir 'build.log')
if ($LASTEXITCODE -ne 0) { throw "Payload build failed" }
$nativeWarnings = Select-String -Path (Join-Path $PayloadDir 'build.log') -Pattern 'warning[:\s]' -CaseSensitive:$false |
    # The shared toolchain flags inject -B<llvm-bin>, which clang-cl reports
    # as an ignored unknown argument for every native target; that driver
    # notice is toolchain-owned, not a source diagnostic.
    Where-Object { $_.Line -notmatch 'unknown argument ignored' }
if ($nativeWarnings) {
    $nativeWarnings | ForEach-Object { Write-Host "    $($_.Line)" }
    throw "Payload build produced $($nativeWarnings.Count) warnings"
}
Write-Host '    payload built warning-free'

Write-Host '--- C# host (ARM Release, .NET Native) ---'
$project = Join-Path $script:AppRoot 'host\RuntimeSmoke.csproj'
$restoreLog = Join-Path $HostObjDir 'restore.log'
$buildLog = Join-Path $HostObjDir 'build.log'
New-Item -ItemType Directory -Path $HostObjDir -Force | Out-Null
& $script:MsBuild $project /t:Restore "/p:MSBuildProjectExtensionsPath=$HostObjDir\" /v:m /nologo "/flp:logfile=$restoreLog;verbosity=minimal" 2>&1 |
    Tee-Object -FilePath (Join-Path $HostObjDir 'restore-console.log')
if ($LASTEXITCODE -ne 0) { throw "Host restore failed; see $restoreLog" }
& $script:MsBuild $project /t:Build /p:Configuration=Release /p:Platform=ARM "/p:OutputPath=$HostDir\" "/p:IntermediateOutputPath=$HostObjDir\" "/p:MSBuildProjectExtensionsPath=$HostObjDir\" "/p:AppxPackageDir=$AppxDir\" /m:4 /v:m /nologo "/flp:logfile=$buildLog;verbosity=minimal" 2>&1 |
    Tee-Object -FilePath (Join-Path $HostObjDir 'build-console.log')
if ($LASTEXITCODE -ne 0) { throw "Host build failed; see $buildLog" }
$hostWarnings = Select-String -Path $buildLog, (Join-Path $HostObjDir 'build-console.log') -Pattern 'warning (CS|MSB|C|Xaml|APPX)' |
    Where-Object { $_.Line -notmatch '0 Warning\(s\)' }
if ($hostWarnings) {
    $hostWarnings | ForEach-Object { Write-Host "    $($_.Line)" }
    throw "Host build produced $($hostWarnings.Count) warnings"
}
Write-Host '    host built warning-free'

$ilc = Get-ChildItem -LiteralPath $HostDir -Recurse -Filter '*.dll' |
    Where-Object { $_.Name -eq 'WebKitRuntimeSmokeHost.dll' }
if (-not $ilc) { throw "Host output is missing the .NET Native compiled image" }
Write-Host "    .NET Native image: $($ilc.FullName) ($($ilc.Length) bytes)"
Write-Host 'RuntimeSmoke payload + host built warning-free.'
