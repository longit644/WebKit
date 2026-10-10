#requires -Version 7.0
<#
.SYNOPSIS
Builds the ARM32 XAML host for controlled M10 Source/state acceptance.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-source-state-probe'),
    [string]$Version = '1.0.0.1',
    [string]$FontDirectory,
    [switch]$InitializationTests,
    [switch]$EventCallbackTests,
    [switch]$CommandTests,
    [switch]$LifecycleTests,
    [switch]$TransactionTests,
    [switch]$SchedulerTests,
    [string]$SchedulerBaseUrl,
    [string]$EngineDirectory,
    [string]$DependencyBaseline,
    [string]$SignThumbprint,
    [switch]$SkipSign
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for a version-matched diagnostic package' }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid package version' }
if (-not $SkipSign -and -not $SignThumbprint) { throw 'Specify -SignThumbprint or -SkipSign' }
if ($SchedulerTests -and (-not $SchedulerBaseUrl -or -not $EngineDirectory -or -not $DependencyBaseline)) {
    throw 'Scheduler tests require -SchedulerBaseUrl, -EngineDirectory and -DependencyBaseline'
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
& (Join-Path $PSScriptRoot 'build-arm32-cx-component.ps1') -OutDir "$OutDir\component" -SourceStateTests -InitializationTests:$InitializationTests -EventCallbackTests:$EventCallbackTests -CommandTests:$CommandTests -LifecycleTests:$LifecycleTests -TransactionTests:$TransactionTests -SchedulerTests:$SchedulerTests -TestPackageVersion $Version
$identity = Get-Content "$OutDir\component\build-identity.json" -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$sdkVersion = $identity.SdkVersion
$cl = Join-Path $tool 'bin\Hostx64\arm\cl.exe'
$link = Join-Path $tool 'bin\Hostx64\arm\link.exe'
$bin = Join-Path $sdk "bin\$sdkVersion\x64"
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$sdkVersion\ucrt;$sdk\Include\$sdkVersion\um;$sdk\Include\$sdkVersion\shared;$sdk\Include\$sdkVersion\winrt"
<# .SYNOPSIS
Runs one discovered compiler/linker/package tool with fail-fast exit-code checks.
#>
function Invoke-ProbeTool([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/ZW', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00', "/AI$tool\lib\x86\store\references", "/AI$sdk\UnionMetadata\$sdkVersion",
        "/FU$OutDir\component\WebKit.winmd", ('/DTEST_PACKAGE_VERSION_W=L"' + $Version + '"'))
    if ($InitializationTests) { $common += '/DTEST_INITIALIZATION_ACTIONS' }
    if ($EventCallbackTests) { $common += '/DTEST_EVENT_CALLBACKS' }
    if ($CommandTests) { $common += '/DTEST_PUBLIC_COMMANDS' }
    if ($LifecycleTests) { $common += '/DTEST_CONTROL_LIFECYCLE' }
    if ($TransactionTests) { $common += '/DTEST_NOTIFICATION_TRANSACTIONS' }
    if ($SchedulerTests) { $common += '/DTEST_FRAME_SCHEDULER'; $common += ('/DTEST_SCHEDULER_BASE=L"' + $SchedulerBaseUrl.TrimEnd('/') + '"') }
    Invoke-ProbeTool $cl ($common + @("/Fo$OutDir\StateProbeApp.obj", "$repo\Tools\uwp\CXStateTests\StateProbeApp.cpp"))
    $libs = @("/libpath:$tool\lib\arm\store", "/libpath:$sdk\Lib\$sdkVersion\um\arm", "/libpath:$sdk\Lib\$sdkVersion\ucrt\arm",
        'vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib', 'vcruntime.lib', 'ucrt.lib', 'OneCoreUAP.lib')
    Invoke-ProbeTool $link (@('/nologo', '/machine:arm', '/appcontainer', '/subsystem:windows', '/WINMD', '/ENTRY:mainCRTStartup',
        "/WINMDFILE:$OutDir\SourceStateProbe.winmd", "/OUT:$OutDir\SourceStateProbe.exe", "$OutDir\StateProbeApp.obj") + $libs)
    $pkg = Join-Path $OutDir 'package'
    New-Item -ItemType Directory -Path "$pkg\Assets" -Force | Out-Null
    Copy-Item "$OutDir\SourceStateProbe.exe", "$OutDir\SourceStateProbe.winmd", "$OutDir\component\WebKit.dll", "$OutDir\component\WebKit.winmd" $pkg
    [xml]$manifest = Get-Content "$repo\Tools\uwp\CXStateTests\AppxManifest.xml"
    $manifest.Package.Identity.Version = $Version
    $manifest.Save("$pkg\AppxManifest.xml")
    Copy-Item "$repo\Websites\webkit.org\favicon.png" "$pkg\Assets\probe.png"
    # Both Source/state and async-init diagnostics validate the actual package defaults.
    if (-not $FontDirectory) { $FontDirectory = Join-Path $repo 'build-arm-minibrowser\package-skia\Fonts' }
    New-Item -ItemType Directory -Path "$pkg\Fonts" -Force | Out-Null
    foreach ($font in @('DejaVuSans.ttf', 'DejaVuSans-Bold.ttf')) {
        Copy-Item (Join-Path $FontDirectory $font) "$pkg\Fonts\$font"
    }
    Copy-Item "$repo\Tools\uwp\NavigationTests\tls\fixture-ca.pem" "$pkg\cacert.pem"
    if ($SchedulerTests) {
        # Import closure uses the actual freshly built engine, never a fabricated provider DLL.
        Invoke-ProbeTool (Get-Command python).Source @("$repo\Tools\uwp\RuntimeSmoke\stage-app.py", '--build', $OutDir, '--host', 'SourceStateProbe.exe',
            '--no-probe', '--manifest', "$pkg\AppxManifest.xml", '--engine', $EngineDirectory, '--prefix', 'C:/vcpkg/installed/arm-uwp-webkit',
            '--override', $DependencyBaseline, '--reader', (Join-Path $identity.VsPath 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'), '--stage', "$OutDir\engine-closure")
        foreach ($dll in (Get-ChildItem "$OutDir\engine-closure" -Filter '*.dll')) { Copy-Item $dll.FullName $pkg }
    }
    Invoke-ProbeTool "$bin\makeappx.exe" @('pack', '/d', $pkg, '/p', "$OutDir\SourceStateProbe-arm.appx", '/o')
    if (-not $SkipSign) { Invoke-ProbeTool "$bin\signtool.exe" @('sign', '/fd', 'SHA256', '/sha1', $SignThumbprint, "$OutDir\SourceStateProbe-arm.appx") }
    "Source/state probe built: $OutDir\SourceStateProbe-arm.appx"
} finally { $env:INCLUDE = $previousInclude }
