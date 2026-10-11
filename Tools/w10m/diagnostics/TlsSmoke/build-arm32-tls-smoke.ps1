<#
.SYNOPSIS
Builds, stages and packages the migrated TlsSmoke diagnostic (M10 task 8.7).
.DESCRIPTION
Builds the unchanged native transport probe payload (WebKitTlsProbe.dll) for
ARM32 W10M with the discovered clang-cl toolchain, builds the C# application
host for ARM Release through .NET Native with the Community 2022 MSBuild, and
packs a signable appx. All outputs live under a fresh -OutDir (default
%TEMP%\opencode\tls-smoke); shared build trees are only read. This script
adds no new shared behavior: it sources Tools/w10m/arm32-w10m-env.ps1 and calls
Tools/w10m/audit-dlls.py without modifying either.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\tls-smoke'),
    [string]$VsPath,
    [string]$MsvcVersion,
    [string]$SdkVersion = '10.0.19041.0',
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$SignThumbprint = '',
    [int]$Parallel = 4
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$source = Join-Path $repo 'Tools\w10m\diagnostics\TlsSmoke'
$OutDir = [IO.Path]::GetFullPath($OutDir)
foreach ($stale in @((Join-Path $OutDir 'payload'), (Join-Path $OutDir 'cs'), (Join-Path $OutDir 'appx'))) {
    if (Test-Path -LiteralPath $stale) { throw "Use a fresh -OutDir; found stale output: $stale" }
}

function Invoke-BuildTool([string]$exe, [string[]]$arguments) {
    # Pass arguments raw: the shell quotes them for the native command line.
    # Pre-quoting would embed literal quote characters (fatal for cmake flags).
    # Path properties end in a doubled backslash: MSBuild requires a trailing
    # slash, and the doubling keeps the auto-added closing quote intact on
    # Windows PowerShell 5.1 as well as PowerShell 7.
    Write-Host "    $exe $($arguments -join ' ')"
    $output = & $exe @arguments 2>&1
    $code = $LASTEXITCODE
    if ($output) { $output | ForEach-Object { Write-Host "    $_" } }
    if ($code -ne 0) { throw "$exe failed: $code" }
    return $output
}

# Community 2022 MSBuild owns the C#/.NET Native build; vswhere discovery
# prefers it, and -VsPath overrides when several instances are installed.
if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $instances = (& $vswhere -all -products '*' -format json | ConvertFrom-Json)
    $community = @($instances | Where-Object { $_.installationPath -like '*\Community' })
    $ordered = @($community) + @($instances | Where-Object { $_.installationPath -notlike '*\Community' })
    foreach ($instance in $ordered) {
        $candidate = Join-Path $instance.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
        $xamlTargets = Join-Path $instance.installationPath 'MSBuild\Microsoft\WindowsXaml\v17.0\Microsoft.Windows.UI.Xaml.CSharp.targets'
        if ((Test-Path -LiteralPath $candidate) -and (Test-Path -LiteralPath $xamlTargets)) {
            $VsPath = $instance.installationPath
            break
        }
    }
}
if (-not $VsPath) { throw 'No VS instance with the W10M C# targets; specify -VsPath' }
$msbuild = Join-Path $VsPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw "Community MSBuild missing: $msbuild" }
Write-Host "MSBuild: $msbuild"
& $msbuild -version | ForEach-Object { Write-Host "    $_" }

# Native ARM32 environment (discovery only; the shared script is unmodified).
$envArgs = @{}
if ($MsvcVersion -ne '') { $envArgs['MsvcVersion'] = $MsvcVersion }
if ($SdkVersion -ne '') { $envArgs['SdkVersion'] = $SdkVersion }
if ($VcpkgRoot -ne '') { $envArgs['VcpkgRoot'] = $VcpkgRoot }
. (Join-Path $repo 'Tools\w10m\arm32-w10m-env.ps1') @envArgs
$clangCl = $env:WK_W10M_CLANG_CL
$llvmBin = Split-Path -Parent ($clangCl -replace '/', '\')
$reader = Join-Path $llvmBin 'llvm-readobj.exe'
Write-Host "Native: $clangCl (SDK $($env:WK_W10M_SDK_VERSION))"

if (-not $VcpkgRoot -or -not (Test-Path -LiteralPath $VcpkgRoot)) {
    if (Test-Path -LiteralPath 'C:\vcpkg') { $VcpkgRoot = 'C:\vcpkg' }
    else { throw 'No vcpkg root; specify -VcpkgRoot' }
}
$vcpkgInstalled = Join-Path $VcpkgRoot 'installed\arm-w10m-webkit'
foreach ($required in @("$vcpkgInstalled\include\curl\curl.h", "$vcpkgInstalled\include\openssl\ssl.h",
        "$vcpkgInstalled\lib\libcurl.lib", "$vcpkgInstalled\lib\libcrypto.lib")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required probe input missing: $required" }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $cmake) { throw 'cmake not on PATH' }

Write-Host '--- native payload (ARM32 W10M) ---'
$payloadBuild = Join-Path $OutDir 'payload\build'
$payloadBin = Join-Path $OutDir 'payload\bin'
New-Item -ItemType Directory -Path $payloadBuild, $payloadBin -Force | Out-Null
Invoke-BuildTool $cmake @('-S', $source, '-B', $payloadBuild, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$repo/WebKitLibraries/toolchains/Toolchain-ARM32-W10M-clang.cmake",
    "-DVCPKG_INSTALLED_DIR=$($vcpkgInstalled -replace '\\', '/')") | Out-Null
$buildLog = Invoke-BuildTool $cmake @('--build', $payloadBuild, '--parallel', "$Parallel")
# Require a warning-free payload build, including toolchain diagnostics.
$clangWarnings = @($buildLog | Where-Object { "$_" -match 'warning[:\]]' })
if ($clangWarnings.Count -gt 0) { throw "Payload built with warnings:`n$($clangWarnings -join "`n")" }
Write-Host '    payload compiled warning-free'
$payloadDll = Join-Path $payloadBuild 'WebKitTlsProbe.dll'
if (-not (Test-Path -LiteralPath $payloadDll)) { throw "Payload DLL missing: $payloadDll" }
Copy-Item $payloadDll $payloadBin
$headers = (& $reader --file-headers $payloadDll | Out-String)
if ($headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT' -or $headers -notmatch 'APPCONTAINER') {
    throw 'Payload is not an ARM32 AppContainer image'
}
Write-Host '    payload is COFF-ARM ARMNT AppContainer'
$exports = (& $reader --coff-exports $payloadDll | Out-String)
foreach ($symbol in @('WKSetTlsSmokeLogPath', 'WKLogTlsSmokeResult', 'WKRunTlsSmokeProbes')) {
    if ($exports -notmatch "(?m)^\s*Name: $([regex]::Escape($symbol))\s*$") { throw "Payload export missing: $symbol" }
    Write-Host "    export $symbol"
}

Write-Host '--- payload dependency closure ---'
$auditJson = Join-Path $OutDir 'payload\closure.json'
$pool = Join-Path $vcpkgInstalled 'bin'
python3 (Join-Path $repo 'Tools\w10m\audit-dlls.py') $payloadDll --search $pool --reader $reader --output $auditJson
$audit = Get-Content $auditJson -Raw | ConvertFrom-Json
foreach ($record in $audit.binaries) {
    if (-not $record.armnt) { throw "Non-ARMNT binary in closure: $($record.path)" }
    $leaf = Split-Path -Leaf $record.path
    if ($leaf -ne 'WebKitTlsProbe.dll') { Copy-Item $record.path $payloadBin }
}
Write-Host "    staged payloads: $((Get-ChildItem $payloadBin -Filter '*.dll').Name -join ', ')"
Write-Host "    external (OS/framework): $($audit.external_dependencies -join ', ')"

Write-Host '--- C# host (ARM Release, .NET Native) ---'
$csOut = Join-Path $OutDir 'cs'
$appxDir = Join-Path $OutDir 'appx'
$msbuildArgs = @((Join-Path $source 'TlsSmoke.csproj'), '/t:restore,build',
    '/p:Configuration=Release', '/p:Platform=ARM', '/m:4', '/v:minimal', '/nologo',
    "/p:OutputPath=$csOut\bin\\", "/p:BaseIntermediateOutputPath=$csOut\obj\\",
    "/p:AppxPackageDir=$appxDir\\", "/p:TlsPayloadDir=$payloadBin\\")
if ($SignThumbprint -ne '') { $msbuildArgs += "/p:PackageCertificateThumbprint=$SignThumbprint" }
$csLog = Invoke-BuildTool $msbuild $msbuildArgs
$csWarnings = @($csLog | Where-Object { "$_" -match ': warning(\s+[A-Z]+[0-9]+)?\s*:' })
if ($csWarnings.Count -gt 0) { throw "C# host built with warnings:`n$($csWarnings -join "`n")" }
Write-Host '    C# host compiled warning-free'
$nativeExe = Join-Path $csOut 'bin\WebKitTlsSmoke.exe'
if (-not (Test-Path -LiteralPath $nativeExe)) { throw "C# output missing: $nativeExe" }
$exeHeaders = (& $reader --file-headers $nativeExe | Out-String)
if ($exeHeaders -notmatch 'IMAGE_FILE_MACHINE_ARMNT') { throw 'C# output is not an ARM image' }
Write-Host '    C# output is a native ARM image'
$ilcEvidence = @(Get-ChildItem $csOut -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)ilc' })
$logEvidence = @($csLog | Where-Object { "$_" -match '(?i)ilc|net native|native compil' })
if ($ilcEvidence.Count -eq 0 -and $logEvidence.Count -eq 0) {
    throw '.NET Native engagement not evidenced; ARM Release must compile through .NET Native'
}
$ilcEvidence | Select-Object -First 3 | ForEach-Object { Write-Host "    .NET Native: $($_.FullName)" }
$assetsFile = Get-ChildItem $csOut -Recurse -Filter 'project.assets.json' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $assetsFile) { throw 'project.assets.json missing; restore did not run' }
$assets = Get-Content $assetsFile.FullName -Raw | ConvertFrom-Json
$uwpVersion = $assets.targets.PSObject.Properties.Name | Where-Object { $_ -match 'UAP' } | Select-Object -First 1
$uwpLibs = $assets.targets.$uwpVersion.PSObject.Properties.Name | Where-Object { $_ -like 'Microsoft.NETCore.UniversalWindowsPlatform/*' }
Write-Host "    restored framework: $($uwpLibs -join ', ')"
if ($uwpLibs -match '/6\.2\.') { throw 'Framework 6.2.x is banned on the phone; restore 6.0.x or older' }

$appx = Get-ChildItem $appxDir -Recurse -Filter '*.appx' | Where-Object { $_.Name -notlike '*upload*' } | Select-Object -First 1
if (-not $appx) { throw "No appx produced under $appxDir" }
Write-Host "    appx: $($appx.FullName) ($([math]::Round($appx.Length / 1KB)) KB)"
$identity = [ordered]@{
    MsvcVersion = $env:WK_W10M_TOOLSET_ROOT; SdkVersion = $env:WK_W10M_SDK_VERSION; VsPath = $VsPath;
    Payload = @(Get-ChildItem $payloadBin -File | ForEach-Object {
        [ordered]@{ Path = $_.Name; SHA256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash } });
    Appx = $appx.Name; AppxSHA256 = (Get-FileHash $appx.FullName -Algorithm SHA256).Hash
}
$identity | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutDir 'build-identity.json')
"TlsSmoke built: $($appx.FullName)"
