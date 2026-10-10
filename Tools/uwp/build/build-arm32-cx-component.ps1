#requires -Version 7.0
<#
.SYNOPSIS
Builds the authored M10 control and its compiler-generated metadata.
.DESCRIPTION
ARM32 uses the Store runtime. The optional x64 build uses the desktop runtime
only for host contract tests, and is never an ARM/device acceptance substitute.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-component-arm32'),
    [string]$VsPath,
    [string]$MsvcVersion,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.19041.0',
    [ValidateSet('arm', 'x64')][string]$Architecture = 'arm',
    [switch]$SourceStateTests,
    [switch]$InitializationTests,
    [switch]$EventCallbackTests,
    [switch]$CommandTests,
    [switch]$LifecycleTests,
    [switch]$TransactionTests,
    [switch]$SchedulerTests,
    [string]$TestPackageVersion = '1.0.0.1'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$api = Join-Path $repo 'Source\WebKit\UIProcess\API\uwp'
$shared = Join-Path $repo 'Source\WebKit\Shared\uwp'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath (Join-Path $OutDir 'WebKit.dll')) { throw 'Use a fresh -OutDir to avoid stale component/metadata output' }

if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    foreach ($instance in (& $vswhere -all -products '*' -format json | ConvertFrom-Json)) {
        $candidates = @(Get-ChildItem (Join-Path $instance.installationPath 'VC\Tools\MSVC') -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName "bin\Hostx64\$Architecture\cl.exe") })
        if ($candidates.Count) { $VsPath = $instance.installationPath; break }
    }
}
if (-not $VsPath) { throw "No VS instance with a $Architecture compiler; specify -VsPath" }
if (-not $MsvcVersion) {
    $MsvcVersion = (Get-ChildItem (Join-Path $VsPath 'VC\Tools\MSVC') -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName "bin\Hostx64\$Architecture\cl.exe") } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
if (-not $SdkRoot) { $SdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
$tool = Join-Path $VsPath "VC\Tools\MSVC\$MsvcVersion"
$sdkInclude = Join-Path $SdkRoot "Include\$SdkVersion"
$metadata = Join-Path $SdkRoot "UnionMetadata\$SdkVersion"
$references = Join-Path $tool 'lib\x86\store\references'
$runtime = Join-Path $tool "lib\$Architecture"
if ($Architecture -eq 'arm') { $runtime = Join-Path $runtime 'store' }
$cl = Join-Path $tool "bin\Hostx64\$Architecture\cl.exe"
$link = Join-Path $tool "bin\Hostx64\$Architecture\link.exe"
$reader = Join-Path $VsPath 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
$winmdidl = Join-Path $SdkRoot "bin\$SdkVersion\x64\winmdidl.exe"
foreach ($required in @($cl, $link, $reader, $winmdidl, "$references\platform.winmd", "$metadata\Windows.winmd",
        "$runtime\vccorlib.lib", "$runtime\msvcprt.lib", "$runtime\msvcrt.lib", "$runtime\vcruntime.lib",
        "$SdkRoot\Lib\$SdkVersion\um\$Architecture\OneCoreUAP.lib", "$SdkRoot\Lib\$SdkVersion\ucrt\$Architecture\ucrt.lib")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required component build input missing: $required" }
}
New-Item -ItemType Directory -Path "$OutDir\obj", "$OutDir\inspect" -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdkInclude\ucrt;$sdkInclude\um;$sdkInclude\shared;$sdkInclude\winrt"
<# .SYNOPSIS
Runs a selected native build/metadata tool and fails on its actual exit code.
#>
function Invoke-ComponentTool([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/ZW', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/D_WINRT_DLL', '/DNOMINMAX',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00', "/AI$references", "/AI$metadata", "/I$api", "/I$shared")
    $files = @("$api\WebKitWebView.cpp", "$api\WebKitWebViewOptions.cpp", "$api\WebKitNavigationEventArgs.cpp",
        "$api\WebKitCertificateFailureToken.cpp", "$api\platform\InitializationActionUWP.cpp", "$api\platform\PageCallbacksUWP.cpp",
        "$api\platform\FrameSchedulerDriverUWP.cpp", "$repo\Tools\uwp\CXComponent\ComponentExports.cpp")
    if ($SourceStateTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"')
        $files += "$repo\Tools\uwp\CXStateTests\SourceStateTests.cpp"
    }
    if ($InitializationTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not $SourceStateTests) { $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"') }
        $files += "$repo\Tools\uwp\CXStateTests\InitializationTests.cpp"
    }
    if ($EventCallbackTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not $SourceStateTests -and -not $InitializationTests) { $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"') }
        $files += "$repo\Tools\uwp\CXStateTests\EventCallbackTests.cpp"
    }
    if ($CommandTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not $SourceStateTests -and -not $InitializationTests -and -not $EventCallbackTests) { $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"') }
        $files += "$repo\Tools\uwp\CXStateTests\CommandTests.cpp"
    }
    if ($LifecycleTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not $SourceStateTests -and -not $InitializationTests -and -not $EventCallbackTests -and -not $CommandTests) { $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"') }
        $files += "$repo\Tools\uwp\CXStateTests\LifecycleTests.cpp"
    }
    if ($TransactionTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not $SourceStateTests -and -not $InitializationTests -and -not $EventCallbackTests -and -not $CommandTests -and -not $LifecycleTests) { $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"') }
        $files += "$repo\Tools\uwp\CXStateTests\TransactionTests.cpp"
    }
    if ($SchedulerTests) {
        if ($TestPackageVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
        if (-not ($SourceStateTests -or $InitializationTests -or $EventCallbackTests -or $CommandTests -or $LifecycleTests -or $TransactionTests)) {
            $common += ('/DTEST_PACKAGE_VERSION="' + $TestPackageVersion + '"')
        }
        $files += "$repo\Tools\uwp\CXStateTests\SchedulerTests.cpp"
    }
    $objects = @()
    foreach ($file in $files) {
        $object = Join-Path $OutDir ('obj\' + [IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        Invoke-ComponentTool $cl ($common + @("/Fo$object", $file))
        $objects += $object
    }
    $nativeFlags = @($common | Where-Object { $_ -notmatch '^/(ZW|AI|D_WINRT_DLL)' })
    $nativeFiles = @("$api\native\WebViewConfigurationUWP.cpp", "$api\native\NavigationStateUWP.cpp", "$api\native\PageControllerUWP.cpp",
        "$api\rendering\SwapChainPresenterUWP.cpp", "$api\rendering\FrameMetricsUWP.cpp", "$api\rendering\FrameSchedulerUWP.cpp",
        "$api\input\ScrollControllerUWP.cpp", "$api\input\InputAdapterUWP.cpp")
    foreach ($file in $nativeFiles) {
        $object = Join-Path $OutDir ('obj\' + [IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        Invoke-ComponentTool $cl ($nativeFlags + @("/Fo$object", $file))
        $objects += $object
    }
    $libs = @("/libpath:$runtime", "/libpath:$SdkRoot\Lib\$SdkVersion\um\$Architecture",
        "/libpath:$SdkRoot\Lib\$SdkVersion\ucrt\$Architecture", 'vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib',
        'vcruntime.lib', 'ucrt.lib', 'OneCoreUAP.lib', 'bcrypt.lib', 'crypt32.lib', 'd3d11.lib', 'dxgi.lib')
    $linkArgs = @('/nologo', '/dll', "/machine:$Architecture", '/WINMD', "/WINMDFILE:$OutDir\WebKit.winmd", "/OUT:$OutDir\WebKit.dll")
    if ($Architecture -eq 'arm') { $linkArgs += '/appcontainer' }
    Invoke-ComponentTool $link ($linkArgs + $objects + $libs)
    Invoke-ComponentTool $winmdidl @("/outdir:$OutDir\inspect", "/metadata_dir:$metadata", "$OutDir\WebKit.winmd")
    $headers = (& $reader --file-headers "$OutDir\WebKit.dll" | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Component PE inspection failed' }
    if ($Architecture -eq 'arm' -and ($headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT' -or $headers -notmatch 'APPCONTAINER')) {
        throw 'Component is not an ARM32 AppContainer DLL'
    }
    $exports = (& $reader --coff-exports "$OutDir\WebKit.dll" | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Component export inspection failed' }
    foreach ($symbol in @('DllGetActivationFactory', 'DllCanUnloadNow')) {
        if ($exports -notmatch "Name: $symbol") { throw "Component export missing: $symbol" }
    }
    if ([bool]($exports -match 'Name: WKUWPTestSourceState') -ne [bool]$SourceStateTests) {
        throw 'Diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestInitialization') -ne [bool]$InitializationTests) {
        throw 'Initialization diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestEventCallbacks') -ne [bool]$EventCallbackTests) {
        throw 'Event callback diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestCommands') -ne [bool]$CommandTests) {
        throw 'Command diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestLifecycle') -ne [bool]$LifecycleTests) {
        throw 'Lifecycle diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestTransactions') -ne [bool]$TransactionTests) {
        throw 'Transaction diagnostic-export presence does not match selected build flavor'
    }
    if ([bool]($exports -match 'Name: WKUWPTestScheduler') -ne [bool]$SchedulerTests) {
        throw 'Scheduler diagnostic-export presence does not match selected build flavor'
    }
    & python (Join-Path $PSScriptRoot 'check-cx-component-metadata.py') --idl "$OutDir\inspect\WebKit.idl" --api $api
    if ($LASTEXITCODE -ne 0) { throw 'M10 metadata contract check failed' }
    $identity = [ordered]@{ ContractRevision = 2; Architecture = $Architecture; SourceStateTests = [bool]$SourceStateTests;
        InitializationTests = [bool]$InitializationTests; EventCallbackTests = [bool]$EventCallbackTests; CommandTests = [bool]$CommandTests;
        LifecycleTests = [bool]$LifecycleTests; TransactionTests = [bool]$TransactionTests; SchedulerTests = [bool]$SchedulerTests; MsvcVersion = $MsvcVersion;
        SdkVersion = $SdkVersion; VsPath = $VsPath; ToolsetRoot = $tool; SdkRoot = $SdkRoot;
        Sources = @(($files + $nativeFiles) | ForEach-Object { [IO.Path]::GetRelativePath($repo, $_).Replace('\', '/') });
        Payload = @('WebKit.dll', 'WebKit.winmd' | ForEach-Object {
            [ordered]@{ Path = $_; SHA256 = (Get-FileHash (Join-Path $OutDir $_) -Algorithm SHA256).Hash }
        }) }
    $identity | ConvertTo-Json -Depth 5 | Set-Content "$OutDir\build-identity.json"
    & python (Join-Path $PSScriptRoot 'check-public-control-contract.py') --identity "$OutDir\build-identity.json" --repository $repo
    if ($LASTEXITCODE -ne 0) { throw 'Public control structured-source contract failed' }
    "M10 component build PASS ($Architecture): $OutDir"
} finally {
    $env:INCLUDE = $previousInclude
}
