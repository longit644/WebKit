#requires -Version 7.0
<#
.SYNOPSIS
Builds the M10 Windows 10 Mobile C# MiniBrowser shell (task 8.1): fresh component metadata into
TEMP, ARM Release .NET Native app build via Community 2022 MSBuild, signing,
and all four verification gates. Never builds inside shared build dirs or
modifies the shared native engine/component trees.

.PARAMETER HostHtmlSmoke
Build a test-only shell that automatically requests the packaged local demo
through the public WebKit control and records its structured events/state in
LocalState/hosthtml-candidate.txt.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\m10-w10m-minibrowser-v1'),
    [string]$Version = '1.0.0.1',
    [string]$SignThumbprint = '18FD65CF5E103CF96F04C2A451DC49C94788A40D',
    [string]$MsBuildPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin',
    [switch]$SkipSign,
    [switch]$LayoutDiagnostics,
    [switch]$HostHtmlSmoke
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$source = Join-Path $repo 'Tools\MiniBrowser\w10m\csharp'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for matching metadata/XBF/package output' }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid shell package version' }
if (-not $SkipSign -and -not $SignThumbprint) { throw 'Specify -SignThumbprint or -SkipSign' }
$msbuild = Join-Path $MsBuildPath 'msbuild.exe'
$sdkBin = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64'
foreach ($required in @($msbuild, "$sdkBin\signtool.exe", "$sdkBin\makeappx.exe", "$source\MiniBrowserW10M.csproj")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required shell build input missing: $required" }
}
<#
.SYNOPSIS
Runs one build tool and checks its actual exit code.
#>
function Invoke-ShellTool([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed: $LASTEXITCODE" }
}

# Gate 4 (static half): VisibleBoundsChanged subscription + margin math wiring.
$codeBehind = Get-Content "$source\MainPage.xaml.cs" -Raw
foreach ($pattern in @('VisibleBoundsChanged\s*\+=', 'VisibleBoundsChanged\s*-=', 'VisibleBoundsHelper\.ComputeBrowserMargins', 'ShellRoot\.Margin')) {
    if ($codeBehind -notmatch $pattern) { throw "Code-behind missing required wiring: $pattern" }
}
# The full-window mode precedes Frame construction; only the shell applies insets.
$appCode = Get-Content "$source\App.xaml.cs" -Raw
if ($appCode -notmatch 'SetDesiredBoundsMode\(ApplicationViewBoundsMode\.UseCoreWindow\)') {
    throw 'App must request UseCoreWindow before Frame navigation so safe-area insets are not doubled'
}
if (($appCode + $codeBehind) -match 'ApplicationViewBoundsMode\.UseVisible') {
    throw 'Automatic UseVisible layout must not be combined with manual visible-bounds margins'
}
if ((Get-Content "$source\App.xaml" -Raw) -match 'RequestedTheme=') {
    throw 'Application chrome must inherit the Windows system theme'
}
# Shell launches with a null Source: no Source attribute may appear in markup.
$markup = Get-Content "$source\MainPage.xaml" -Raw
if ($markup -match 'Source=') { throw 'MainPage.xaml must not declare a Source (null at startup)' }
# Additional actions are a root-docked app-bar panel rather than a detached context menu.
if ($markup -match '<MenuFlyout|<Button\.Flyout') { throw 'Use the bottom app-bar panel, not a context-menu Flyout' }
$appBarMarkup = Get-Content "$source\Views\Controls\BrowserAppBarControl.xaml" -Raw
$addressBarMarkup = Get-Content "$source\Views\Controls\AddressBarControl.xaml" -Raw
$menuMarkup = $markup + $appBarMarkup + $addressBarMarkup
foreach ($pattern in @('x:Name="MenuSheetElement"', 'x:Name="MenuOverlayElement"', 'ToggleMenuCommand', 'CloseMenuCommand')) {
    if ($menuMarkup -notmatch $pattern) { throw "Bottom menu is missing required MVVM wiring: $pattern" }
}
# Gate 3 (static half): no framework .cpp may be referenced by the shell project
# (comments stripped so the documenting comment itself cannot trip the gate).
$project = [regex]::Replace((Get-Content "$source\MiniBrowserW10M.csproj" -Raw), '<!--.*?-->', '', 'Singleline')
foreach ($pattern in @('<ClCompile', '\.cpp"', 'Source/WebKit')) {
    if ($project -match $pattern) { throw "Shell project references forbidden native source: $pattern" }
}

# Fresh component metadata into TEMP (allowed); the shell only reads it.
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
& (Join-Path $repo 'Tools\w10m\build\build-arm32-cx-component.ps1') -OutDir "$OutDir\component"
foreach ($output in @("$OutDir\component\WebKit.winmd", "$OutDir\component\WebKit.dll")) {
    if (-not (Test-Path -LiteralPath $output)) { throw "Component output missing: $output" }
}
# Metadata-only reference directory: the WinMD without its DLL beside it, so
# assembly resolution cannot mistake the native implementation for metadata.
$metadataDir = "$OutDir\metadata\"
New-Item -ItemType Directory -Path $metadataDir -Force | Out-Null
Copy-Item "$OutDir\component\WebKit.winmd" $metadataDir -Force

# Gate 4 (executable half): host-runnable pure-math check.
& (Join-Path $source 'test-visible-bounds-math.ps1') -MsBuildPath $MsBuildPath

# Build the C# sources from a TEMP copy so no bin/obj/AppPackages lands in the repo.
$buildSrc = "$OutDir\src"
New-Item -ItemType Directory -Path $buildSrc -Force | Out-Null
Copy-Item "$source\*" $buildSrc -Recurse -Force
[xml]$manifest = Get-Content "$buildSrc\Package.appxmanifest"
$manifest.Package.Identity.Version = $Version
$manifest.Save("$buildSrc\Package.appxmanifest")
$logFile = "$OutDir\msbuild.log"
$shellBuildArgs = @("$buildSrc\MiniBrowserW10M.csproj", '/t:Rebuild', '/p:Configuration=Release', '/p:Platform=ARM',
    '/m:4', '/v:minimal', '/restore', "/p:WebKitMetadataDir=$metadataDir", "/p:WebKitBinaryDir=$OutDir\component\", '/p:AppxBundle=Never',
    "/fl", "/flp:logfile=$logFile;verbosity=normal")
if ($LayoutDiagnostics) { $shellBuildArgs += '/p:LayoutDiagnostics=true' }
if ($HostHtmlSmoke) { $shellBuildArgs += '/p:HostHtmlSmoke=true' }
Invoke-ShellTool $msbuild $shellBuildArgs
$log = Get-Content $logFile -Raw
# Gate 3 (build half): no native compiler invocation in the shell build.
if ($log -match 'cl\.exe' -or $log -match 'ClCompile') { throw 'Shell build invoked the native compiler' }
# Gate 1: ARM Release warning-free with .NET Native engaged.
$warnings = ([regex]::Matches($log, ': warning [A-Z]+\d+:')).Count
if ($log -notmatch '0 Warning\(s\)') { throw "Shell build is not warning-free ($warnings warnings)" }
if ($warnings -ne 0) { throw "Shell build warning count mismatch: $warnings" }
if ($log -notmatch 'ilc\.exe|Running .NET Native|DotNetNative') { throw '.NET Native engagement not found in build log' }
$ilcDir = Get-ChildItem "$buildSrc\obj\ARM\Release" -Directory -Filter 'ilc' -ErrorAction SilentlyContinue
if (-not $ilcDir) { throw '.NET Native ilc output directory missing' }
# Gate 2: compiled XAML constructs WebKitWebView from WebKit.winmd.
$xbf = "$buildSrc\obj\ARM\Release\MainPage.xbf"
$provider = "$buildSrc\obj\ARM\Release\XamlTypeInfo.g.cs"
$pageGen = "$buildSrc\obj\ARM\Release\MainPage.g.cs"
foreach ($generated in @($xbf, $provider, $pageGen)) {
    if (-not (Test-Path -LiteralPath $generated)) { throw "Generated XAML output missing: $generated" }
}
# The XBF stores hashed type names (no plaintext), so the construction proof
# is the generated provider's type table plus the generated Connect cast;
# the XBF itself is proven present, non-trivial and packaged below.
$xbfBytes = [IO.File]::ReadAllBytes($xbf)
if ($xbfBytes.Length -lt 256) { throw 'MainPage.xbf is unexpectedly small' }
"MainPage.xbf bytes: $($xbfBytes.Length)"
$providerText = (Get-Content $provider -Raw) + (Get-Content $pageGen -Raw)
foreach ($member in @('WebKit.WebKitWebView', 'global::WebKit.WebKitWebView')) {
    if ($providerText -notmatch [regex]::Escape($member)) { throw "Generated provider missing member: $member" }
}
# The .NET Native toolchain rooted the public control type from compiled markup.
$xr = "$buildSrc\bin\ARM\Release\WebKitMiniBrowser.xr.xml"
if (-not (Test-Path -LiteralPath $xr)) { throw 'Native interop report missing' }
if ((Get-Content $xr -Raw) -notmatch '<RootType FullName="WebKit\.WebKitWebView"') { throw 'Native report does not root WebKit.WebKitWebView' }
# Package: the Implementation metadata stages WebKit.dll into the MSBuild appx
# with registration. The WinMD itself is staged only if the build did not
# already deploy it beside the executable.
$appx = Get-ChildItem $buildSrc -Filter '*.appx' -Recurse | Where-Object { $_.FullName -match 'AppPackages' } | Select-Object -First 1
if (-not $appx) { $appx = Get-ChildItem $buildSrc -Filter '*.appx' -Recurse | Select-Object -First 1 }
if (-not $appx) { throw 'No appx produced by the shell build' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Get-AppxEntries([string]$path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($path)
    try { return @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
}
$portable = "$OutDir\MiniBrowserW10M-arm.appx"
$entries = Get-AppxEntries $appx.FullName
if (-not ($entries -match 'WebKit\.winmd$')) {
    $pkg = "$OutDir\package"
    New-Item -ItemType Directory -Path $pkg -Force | Out-Null
    Invoke-ShellTool "$sdkBin\makeappx.exe" @('unpack', '/p', $appx.FullName, '/d', $pkg, '/o')
    Copy-Item "$OutDir\component\WebKit.winmd" $pkg -Force
    Invoke-ShellTool "$sdkBin\makeappx.exe" @('pack', '/d', $pkg, '/p', $portable, '/o')
} else {
    Copy-Item $appx.FullName $portable -Force
}
if (-not $SkipSign) { Invoke-ShellTool "$sdkBin\signtool.exe" @('sign', '/fd', 'SHA256', '/sha1', $SignThumbprint, $portable) }
Invoke-ShellTool "$sdkBin\signtool.exe" @('verify', '/pa', $portable)
$entries = Get-AppxEntries $portable
# .NET Native bakes compiled XAML into the native binary (the proof package
# likewise ships no .xbf); the obj XBF + provider + xr.xml above prove the
# markup construction instead.
foreach ($required in @('WebKit.dll', 'WebKit.winmd', 'WebKitMiniBrowser.exe', 'AppxManifest.xml', 'resources.pri')) {
    if (-not ($entries -match ([regex]::Escape($required) + '$'))) { throw "Signed package missing payload: $required" }
}
$zip = [IO.Compression.ZipFile]::OpenRead($portable)
try {
    $entry = $zip.GetEntry('AppxManifest.xml')
    $reader = [IO.StreamReader]::new($entry.Open())
    try { $packagedManifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
    foreach ($pattern in @('WebKit\.dll', 'WebKit\.WebKitWebView', 'WebKit\.WebKitWebViewOptions', 'PhoneIdentity', 'internetClient')) {
        if ($packagedManifest -notmatch $pattern) { throw "Packaged manifest missing registration: $pattern" }
    }
} finally { $zip.Dispose() }
$identity = [ordered]@{ Version = $Version; Package = 'WebKitProbe.MiniBrowser'; LayoutDiagnostics = [bool]$LayoutDiagnostics; HostHtmlSmoke = [bool]$HostHtmlSmoke;
    Payload = @(Get-ChildItem $OutDir -Recurse -File | ForEach-Object {
        [ordered]@{ Path = $_.FullName.Substring($OutDir.Length + 1); SHA256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
    }) }
$identity | ConvertTo-Json -Depth 5 | Set-Content "$OutDir\build-identity.json"
"MiniBrowser W10M shell PASS: $portable"
