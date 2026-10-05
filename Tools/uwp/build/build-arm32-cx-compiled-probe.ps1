<#
.SYNOPSIS
Builds the compiled CX/XBF phone probe from authored repository sources.
.DESCRIPTION
Generates matching component metadata from the same ARM /ZW object used for its
DLL, runs both SDK XAML passes, then stages XBF-only resources and a package-matched PRI.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-compiled-probe'),
    [string]$VsPath,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.19041.0',
    [string]$MsvcVersion,
    [string]$FrameworkRef,
    [string]$SignThumbprint,
    [switch]$SkipSign
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$source = Join-Path $repo 'Tools\uwp\CXCompiledProbe'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (Test-Path (Join-Path $OutDir 'package')) { throw 'Use a fresh -OutDir; an existing package may contain stale payloads' }

# VS discovery selects an instance that actually has an ARM compiler.
if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $instances = (& $vswhere -all -products '*' -format json | ConvertFrom-Json)
    foreach ($instance in $instances) {
        if (Get-ChildItem (Join-Path $instance.installationPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName 'bin\Hostx64\arm\cl.exe') }) {
            $VsPath = $instance.installationPath
            break
        }
    }
}
if (-not $VsPath) { throw 'No VS instance with an ARM32 compiler; specify -VsPath' }
if (-not $MsvcVersion) {
    $MsvcVersion = (Get-ChildItem (Join-Path $VsPath 'VC\Tools\MSVC') -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'bin\Hostx64\arm\cl.exe') } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
if (-not $SdkRoot) { $SdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
$SdkRoot = $SdkRoot.TrimEnd('\') + '\'
$tool = Join-Path $VsPath "VC\Tools\MSVC\$MsvcVersion"
$bin = Join-Path $SdkRoot "bin\$SdkVersion\x64"
if (-not $FrameworkRef) {
    $frameworkRoot = Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\UWPNuGetPackages\microsoft.netcore.universalwindowsplatform'
    $FrameworkRef = Get-ChildItem $frameworkRoot -Filter mscorlib.dll -Recurse |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty DirectoryName
}
$cl = Join-Path $tool 'bin\Hostx64\arm\cl.exe'
$link = Join-Path $tool 'bin\Hostx64\arm\link.exe'
$msbuild = Join-Path $VsPath 'MSBuild\Current\Bin\MSBuild.exe'
$reader = Join-Path $VsPath 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
foreach ($required in @($cl, $link, $msbuild, $reader, "$tool\lib\x86\store\references\platform.winmd",
        "$SdkRoot\Lib\$SdkVersion\um\arm\OneCoreUAP.lib", "$SdkRoot\Lib\$SdkVersion\ucrt\arm\ucrt.lib",
        "$FrameworkRef\mscorlib.dll", "$bin\makepri.exe", "$bin\makeappx.exe", "$bin\winmdidl.exe",
        "$repo\Tools\uwp\lib\arm-crtstart.lib", "$repo\Tools\uwp\lib\clang_rt.builtins-arm.lib")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required ARM/CX build input missing: $required" }
}
if (-not $SkipSign -and -not $SignThumbprint) { throw 'Specify -SignThumbprint or -SkipSign' }
New-Item -ItemType Directory -Path "$OutDir\component", "$OutDir\gen" -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$SdkRoot\Include\$SdkVersion\ucrt;$SdkRoot\Include\$SdkVersion\um;$SdkRoot\Include\$SdkVersion\shared;$SdkRoot\Include\$SdkVersion\winrt"

# Never trust only a tool's banner; fail immediately on its exit code.
function Invoke-BuildTool([string]$exe, [string[]]$arguments) {
    # Windows PowerShell 5.1 must escape a trailing backslash before a closing quote.
    $quoted = @($arguments | ForEach-Object { '"' + ($_ -replace '(\\+)$', '$1$1') + '"' })
    & $exe @quoted
    if ($LASTEXITCODE -ne 0) { throw "$exe failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo','/c','/ZW','/EHsc','/MD','/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP','/D_WIN32_WINNT=0x0A00',
        "/AI$tool\lib\x86\store\references", "/AI$SdkRoot\UnionMetadata\$SdkVersion")
    $libs = @("/libpath:$tool\lib\arm\store", "/libpath:$SdkRoot\Lib\$SdkVersion\um\arm", "/libpath:$SdkRoot\Lib\$SdkVersion\ucrt\arm",
        'vccorlib.lib','msvcprt.lib','msvcrt.lib','vcruntime.lib','OneCoreUAP.lib')
    Invoke-BuildTool $cl ($common + @("/Fo$OutDir\component\probe.obj", "$repo\Tools\uwp\CXComponent\XamlProbeComponent.cpp"))
    Invoke-BuildTool $link (@('/nologo','/dll','/machine:arm','/appcontainer','/WINMD',
        "/WINMDFILE:$OutDir\component\XamlProbeCX.winmd", "/OUT:$OutDir\component\XamlProbeComponent.dll",
        '/export:DllGetActivationFactory','/export:DllCanUnloadNow', "$OutDir\component\probe.obj") + $libs)
    $properties = @("/p:SourceRoot=$source", "/p:OutputRoot=$OutDir", "/p:VsRoot=$VsPath", "/p:SdkRoot=$SdkRoot", "/p:SdkVersion=$SdkVersion", "/p:FrameworkRef=$FrameworkRef")
    Invoke-BuildTool $msbuild (@("$source\XamlCompile.proj", '/t:Pass1', '/v:minimal') + $properties)
    $appCompile = $common + @("/FU$OutDir\component\XamlProbeCX.winmd", "/I$OutDir\gen", "/I$source")
    Invoke-BuildTool $cl ($appCompile + @("/Fo$OutDir\seed.obj", "$source\Seed.cpp"))
    Invoke-BuildTool $link (@('/nologo','/dll','/noentry','/machine:arm','/appcontainer','/WINMD:ONLY',
        "/WINMDFILE:$OutDir\CXCompileProbe.winmd", "$OutDir\seed.obj") + $libs)
    Invoke-BuildTool $msbuild (@("$source\XamlCompile.proj", '/t:Pass2', '/v:minimal') + $properties)
    $objects = @()
    foreach ($file in @("$source\App.cpp", "$source\ProbePage.cpp", "$source\Diagnostics.cpp",
            "$OutDir\gen\XamlTypeInfo.g.cpp", "$OutDir\gen\XamlTypeInfo.Impl.g.cpp", "$OutDir\gen\XamlLibMetadataProvider.g.cpp")) {
        $object = Join-Path $OutDir ((Split-Path -Leaf $file) + '.obj')
        Invoke-BuildTool $cl ($appCompile + @("/Fo$object", $file))
        $objects += $object
    }
    # Store CRT precedes repository native helpers so its CX-aware startup wins.
    Invoke-BuildTool $link (@('/nologo','/machine:arm','/appcontainer','/subsystem:windows','/WINMD','/ENTRY:mainCRTStartup',
        "/WINMDFILE:$OutDir\CXCompileProbe.winmd", "/OUT:$OutDir\XamlCompiledProbeHost.exe", "/libpath:$repo\Tools\uwp\lib",
        "/libpath:$tool\lib\arm\store") + $objects + $libs + @('ucrt.lib','runtimeobject.lib','ole32.lib','kernel32.lib','arm-crtstart.lib','clang_rt.builtins-arm.lib'))
    $pkg = Join-Path $OutDir 'package'
    if (Test-Path $pkg) { throw 'Package staging already exists; use a fresh -OutDir to avoid stale payloads' }
    New-Item -ItemType Directory -Path "$pkg\CXCompileProbe", "$pkg\Assets" -Force | Out-Null
    Copy-Item "$OutDir\XamlCompiledProbeHost.exe", "$OutDir\CXCompileProbe.winmd", "$OutDir\component\XamlProbeCX.winmd", "$OutDir\component\XamlProbeComponent.dll" $pkg
    Copy-Item "$source\AppxManifest.xml" $pkg
    Copy-Item "$OutDir\gen\App.xbf", "$OutDir\gen\ProbePage.xbf" "$pkg\CXCompileProbe"
    Copy-Item "$repo\Websites\webkit.org\favicon.png" "$pkg\Assets\probe.png"
    foreach ($binary in @('XamlCompiledProbeHost.exe', 'XamlProbeComponent.dll')) {
        $headers = (& $reader --file-headers (Join-Path $pkg $binary) | Out-String)
        if ($headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT' -or $headers -notmatch 'APPCONTAINER') { throw "Invalid ARM32 AppContainer: $binary" }
    }
    $exports = (& $reader --coff-exports "$pkg\XamlProbeComponent.dll" | Out-String)
    foreach ($symbol in @('DllGetActivationFactory', 'DllCanUnloadNow')) {
        if ($exports -notmatch "Name: $symbol") { throw "Component export missing: $symbol" }
    }
    $imports = (& $reader --coff-imports "$pkg\XamlCompiledProbeHost.exe" "$pkg\XamlProbeComponent.dll" | Out-String)
    $imports | Set-Content (Join-Path $OutDir 'imports.txt')
    # This browser-free package has only two native images. Every other import
    # must be an OS contract or an ARM VCLibs framework DLL resolved on the phone.
    $dependencies = @([regex]::Matches($imports, '(?m)^\s+Name: ([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    foreach ($dependency in $dependencies) {
        if ($dependency -notmatch '^(api-ms-win-.*\.dll|KERNEL32\.dll|ole32\.dll|vccorlib140_app\.dll|MSVCP140_APP\.dll|VCRUNTIME140_APP\.dll)$') {
            throw "Unresolved/unaudited import in browser-free package: $dependency"
        }
    }
    New-Item -ItemType Directory -Path "$OutDir\inspect-component" -Force | Out-Null
    Invoke-BuildTool "$bin\winmdidl.exe" @("/outdir:$OutDir\inspect-component", "/metadata_dir:$SdkRoot\UnionMetadata\$SdkVersion", "$pkg\XamlProbeCX.winmd")
    $contract = Get-Content "$OutDir\inspect-component\WebKitProbe.idl" -Raw
    if ($contract -notmatch 'runtimeclass XamlProbe : Windows.UI.Xaml.Controls.UserControl' -or
        $contract -notmatch '\[propget\] HRESULT ProbeText' -or $contract -notmatch '\[propput\] HRESULT ProbeText' -or
        $contract -notmatch '6D1C8F44-2B90-4A37-8C65-1F9D3B7A5E20') { throw 'CX metadata identity/property inspection failed' }
    Invoke-BuildTool "$bin\makepri.exe" @('createconfig','/cf',"$OutDir\priconfig.xml",'/dq','en-US','/pv','10.0.0','/o')
    Invoke-BuildTool "$bin\makepri.exe" @('new','/pr',$pkg,'/cf',"$OutDir\priconfig.xml",'/in','WebKitProbe.XamlCompiledHost','/of',"$OutDir\resources.pri",'/o')
    Copy-Item "$OutDir\resources.pri" "$pkg\resources.pri"
    Invoke-BuildTool "$bin\makepri.exe" @('dump','/if',"$pkg\resources.pri",'/of',"$OutDir\resources-dump.xml",'/dt','Detailed','/o')
    $resources = Get-Content "$OutDir\resources-dump.xml" -Raw
    if ($resources -notmatch 'ResourceMap name="WebKitProbe.XamlCompiledHost"' -or $resources -notmatch 'CXCompileProbe\\ProbePage.xbf') {
        throw 'PRI map identity or compiled page resource mismatch'
    }
    # Record selected build inputs and payload hashes independently of signing.
    $identity = [ordered]@{ MsvcVersion = $MsvcVersion; SdkVersion = $SdkVersion; VsPath = $VsPath;
        FrameworkRef = $FrameworkRef; Imports = $dependencies; Payload = @(Get-ChildItem $pkg -Recurse -File | ForEach-Object {
            [ordered]@{ Path = $_.FullName.Substring($pkg.Length + 1); SHA256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
        }) }
    $identity | ConvertTo-Json -Depth 5 | Set-Content "$OutDir\build-identity.json"
    Invoke-BuildTool "$bin\makeappx.exe" @('pack','/d',$pkg,'/p',"$OutDir\XamlCompiledProbe-arm.appx",'/o')
    if (-not $SkipSign) { Invoke-BuildTool "$bin\signtool.exe" @('sign','/fd','SHA256','/sha1',$SignThumbprint,"$OutDir\XamlCompiledProbe-arm.appx") }
    "Compiled CX probe built: $OutDir\XamlCompiledProbe-arm.appx"
} finally {
    $env:INCLUDE = $previousInclude
}
