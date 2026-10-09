#requires -Version 7.0
<#
.SYNOPSIS
Builds matching ARM32 WebKit metadata and a compiled-XAML public-control contract host.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\webview-compiled-probe'),
    [string]$Version = '1.0.0.1',
    [string]$FrameworkRef,
    [string]$FontDirectory,
    [string]$SignThumbprint,
    [switch]$SkipSign
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$source = Join-Path $repo 'Tools\uwp\CXWebViewCompiledProbe'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for matching metadata/XBF/package output' }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid diagnostic package version' }
if (-not $SkipSign -and -not $SignThumbprint) { throw 'Specify -SignThumbprint or -SkipSign' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
& (Join-Path $PSScriptRoot 'build-arm32-cx-component.ps1') -OutDir "$OutDir\component"
$component = Get-Content "$OutDir\component\build-identity.json" -Raw | ConvertFrom-Json
$tool = $component.ToolsetRoot
$sdk = $component.SdkRoot.TrimEnd('\') + '\'
$sdkVersion = $component.SdkVersion
$vs = $component.VsPath
$bin = Join-Path $sdk "bin\$sdkVersion\x64"
$cl = Join-Path $tool 'bin\Hostx64\arm\cl.exe'
$link = Join-Path $tool 'bin\Hostx64\arm\link.exe'
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
$reader = Join-Path $vs 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
if (-not $FrameworkRef) {
    $frameworkRoot = Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\UWPNuGetPackages\microsoft.netcore.universalwindowsplatform'
    $FrameworkRef = Get-ChildItem $frameworkRoot -Filter mscorlib.dll -Recurse |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty DirectoryName
}
if (-not $FontDirectory) { $FontDirectory = Join-Path $repo 'build-arm-minibrowser\package-skia\Fonts' }
foreach ($required in @($msbuild, "$FrameworkRef\mscorlib.dll", "$bin\makepri.exe", "$bin\makeappx.exe", "$bin\signtool.exe",
        "$FontDirectory\DejaVuSans.ttf", "$FontDirectory\DejaVuSans-Bold.ttf")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required compiled contract input missing: $required" }
}
New-Item -ItemType Directory -Path "$OutDir\gen" -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$sdkVersion\ucrt;$sdk\Include\$sdkVersion\um;$sdk\Include\$sdkVersion\shared;$sdk\Include\$sdkVersion\winrt"
<#
.SYNOPSIS
Runs one discovered build/resource/package tool and checks its actual exit code.
#>
function Invoke-CompiledContractTool([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/ZW', '/EHsc', '/MD', '/std:c++17', '/DNOMINMAX',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00', "/AI$tool\lib\x86\store\references", "/AI$sdk\UnionMetadata\$sdkVersion",
        ('/DCOMPILED_PROBE_VERSION="' + $Version + '"'), ('/DCOMPILED_PROBE_VERSION_W=L"' + $Version + '"'))
    $libs = @("/libpath:$tool\lib\arm\store", "/libpath:$sdk\Lib\$sdkVersion\um\arm", "/libpath:$sdk\Lib\$sdkVersion\ucrt\arm",
        'vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib', 'vcruntime.lib', 'ucrt.lib', 'OneCoreUAP.lib')
    $properties = @("/p:SourceRoot=$source", "/p:OutputRoot=$OutDir", "/p:VsRoot=$vs", "/p:SdkRoot=$sdk", "/p:SdkVersion=$sdkVersion", "/p:FrameworkRef=$FrameworkRef")
    Invoke-CompiledContractTool $msbuild (@("$source\XamlCompile.proj", '/t:Pass1', '/v:minimal') + $properties)
    $consumer = $common + @("/FU$OutDir\component\WebKit.winmd", "/I$OutDir\gen", "/I$source")
    Invoke-CompiledContractTool $cl ($consumer + @("/Fo$OutDir\seed.obj", "$source\Seed.cpp"))
    Invoke-CompiledContractTool $link (@('/nologo', '/dll', '/noentry', '/machine:arm', '/appcontainer', '/WINMD:ONLY',
        "/WINMDFILE:$OutDir\WebKitCompiledProbe.winmd", "$OutDir\seed.obj") + $libs)
    Invoke-CompiledContractTool $msbuild (@("$source\XamlCompile.proj", '/t:Pass2', '/v:minimal') + $properties)
    $objects = @()
    foreach ($file in @("$source\App.cpp", "$source\ContractPage.cpp", "$source\Diagnostics.cpp",
            "$OutDir\gen\XamlTypeInfo.g.cpp", "$OutDir\gen\XamlTypeInfo.Impl.g.cpp", "$OutDir\gen\XamlLibMetadataProvider.g.cpp")) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileName($file) + '.obj')
        Invoke-CompiledContractTool $cl ($consumer + @("/Fo$object", $file))
        $objects += $object
    }
    Invoke-CompiledContractTool $link (@('/nologo', '/machine:arm', '/appcontainer', '/subsystem:windows', '/WINMD', '/ENTRY:mainCRTStartup',
        "/WINMDFILE:$OutDir\WebKitCompiledProbe.winmd", "/OUT:$OutDir\WebKitCompiledProbe.exe") + $objects + $libs)
    $pkg = Join-Path $OutDir 'package'
    New-Item -ItemType Directory -Path "$pkg\WebKitCompiledProbe", "$pkg\Assets", "$pkg\Fonts" -Force | Out-Null
    Copy-Item "$OutDir\WebKitCompiledProbe.exe", "$OutDir\WebKitCompiledProbe.winmd", "$OutDir\component\WebKit.dll", "$OutDir\component\WebKit.winmd" $pkg
    Copy-Item "$OutDir\gen\App.xbf", "$OutDir\gen\ContractPage.xbf" "$pkg\WebKitCompiledProbe"
    Copy-Item "$repo\Websites\webkit.org\favicon.png" "$pkg\Assets\probe.png"
    Copy-Item "$FontDirectory\DejaVuSans.ttf", "$FontDirectory\DejaVuSans-Bold.ttf" "$pkg\Fonts"
    Copy-Item "$repo\Tools\uwp\NavigationTests\tls\fixture-ca.pem" "$pkg\cacert.pem"
    [xml]$manifest = Get-Content "$source\AppxManifest.xml"
    $manifest.Package.Identity.Version = $Version
    $manifest.Save("$pkg\AppxManifest.xml")
    Invoke-CompiledContractTool "$bin\makepri.exe" @('createconfig', '/cf', "$OutDir\priconfig.xml", '/dq', 'en-US', '/pv', '10.0.0', '/o')
    Invoke-CompiledContractTool "$bin\makepri.exe" @('new', '/pr', $pkg, '/cf', "$OutDir\priconfig.xml", '/in', 'WebKitProbe.WebKitCompiledHost', '/of', "$pkg\resources.pri", '/o')
    Invoke-CompiledContractTool "$bin\makepri.exe" @('dump', '/if', "$pkg\resources.pri", '/of', "$OutDir\resources-dump.xml", '/dt', 'Detailed', '/o')
    $resources = Get-Content "$OutDir\resources-dump.xml" -Raw
    if ($resources -notmatch 'ResourceMap name="WebKitProbe.WebKitCompiledHost"' -or $resources -notmatch 'WebKitCompiledProbe\\ContractPage.xbf') {
        throw 'Compiled contract PRI identity/page resource mismatch'
    }
    $generated = Get-Content "$OutDir\gen\XamlTypeInfo.g.cpp" -Raw
    $generated += Get-Content "$OutDir\gen\XamlTypeInfo.Impl.g.cpp" -Raw
    foreach ($member in @('WebKit.WebKitWebView', 'DocumentTitle', 'CanGoBack', 'IsLoading', 'Source')) {
        if ($generated -notmatch [regex]::Escape($member)) { throw "Generated provider missing public contract member: $member" }
    }
    foreach ($image in @('WebKitCompiledProbe.exe', 'WebKit.dll')) {
        $headers = (& $reader --file-headers (Join-Path $pkg $image) | Out-String)
        if ($LASTEXITCODE -ne 0 -or $headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT' -or $headers -notmatch 'APPCONTAINER') { throw "Invalid ARM32 compiled contract image: $image" }
    }
    $imports = (& $reader --coff-imports "$pkg\WebKitCompiledProbe.exe" "$pkg\WebKit.dll" | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Compiled contract import inspection failed' }
    $imports | Set-Content "$OutDir\imports.txt"
    $identity = [ordered]@{ Version = $Version; ContractRevision = 2; Component = $component; FrameworkRef = $FrameworkRef;
        Payload = @(Get-ChildItem $pkg -Recurse -File | ForEach-Object {
            [ordered]@{ Path = $_.FullName.Substring($pkg.Length + 1); SHA256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
        }) }
    $identity | ConvertTo-Json -Depth 8 | Set-Content "$OutDir\build-identity.json"
    Invoke-CompiledContractTool "$bin\makeappx.exe" @('pack', '/d', $pkg, '/p', "$OutDir\WebKitCompiledProbe-arm.appx", '/o')
    if (-not $SkipSign) { Invoke-CompiledContractTool "$bin\signtool.exe" @('sign', '/fd', 'SHA256', '/sha1', $SignThumbprint, "$OutDir\WebKitCompiledProbe-arm.appx") }
    "Compiled public WebKit contract built: $OutDir\WebKitCompiledProbe-arm.appx"
} finally { $env:INCLUDE = $previousInclude }
