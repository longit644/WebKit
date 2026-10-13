<#
.SYNOPSIS
Clean-builds the M10 native engine, CX component and CX app from explicit discovery (task 10.3).

.DESCRIPTION
Single orchestration over the three M10 build domains:

  * Native engine: fresh CMake/Ninja configure of the repository plus
    named targets (default WebCore, JavaScriptCore; eight jobs by default),
    seeded only with measured feature/dependency selections from the preserved
    cache via prepare-arm32-engine-cache.ps1; compiler flags/RC are rediscovered.
  * CX component: build-arm32-cx-component.ps1 into a fresh directory with
    explicit -VsPath/-MsvcVersion/-SdkRoot/-SdkVersion and its
    build-identity.json.
  * CX app: build-arm32-cx-compiled-probe.ps1 into a fresh directory with the
    same explicit versions plus -SkipSign (default) or -SignThumbprint.

Discovery runs once through discover-arm32-toolchain.ps1 -Json; the resolved
toolset/SDK are then passed EXPLICITLY to every child command, so evidence
never hides which paths were used. Every output directory must be fresh: the
script refuses to reuse an existing -OutRoot, and each phase gets its own new
subdirectory, so no incremental state can leak between runs.

No developer-home path is embedded. The repository root derives from this
script's location, the VS installation from vswhere discovery (or -VsPath),
the SDK root from the registry (or -SdkRoot), and all outputs default under
$env:TEMP. Use -DryRun to print the exact resolved commands without executing.

.PARAMETER OutRoot
Fresh orchestration root receiving discovery.json, native/, component/,
apps/ and m10-build-identity.json. Must not exist yet.

.PARAMETER VsPath
Explicit Visual Studio installation root. When omitted, the first vswhere
instance with an ARM32 compiler is selected and recorded.

.PARAMETER MsvcVersion
Explicit MSVC toolset directory name (for example 14.44.35207). When omitted,
the newest ARM32 toolset under the selected installation is used and recorded.

.PARAMETER SdkRoot
Explicit Windows Kits root. When omitted, the registry KitsRoot10 is used.

.PARAMETER SdkVersion
Windows SDK version. Defaults to the pinned 10.0.19041.0, the newest SDK that
still ships ARM32 um/ucrt libraries.

.PARAMETER FrameworkRef
Explicit UWP framework reference directory for the CX app phase. When omitted,
the app script's own discovery applies and is recorded by that phase.

.PARAMETER SignThumbprint
Development certificate thumbprint for the CX app package. Requires -SkipSign
to be absent.

.PARAMETER SkipSign
Skip app package signing (default signing behavior of this orchestration: the
app child script requires either -SignThumbprint or -SkipSign, and this
orchestration defaults to -SkipSign unless a thumbprint is supplied).

.PARAMETER NativeTargets
Native named targets. Default WebCore and JavaScriptCore, the engine payload the
staging audits close over.

.PARAMETER NativeParallel
Maximum parallel native build jobs (default 8). Lower this on memory-limited
hosts; the resolved value is recorded in the build-identity manifest.

.PARAMETER HostHtmlCandidate
Build the unverified local HostHtml phone-test candidate. This opt-in defines
WKW10M_HOSTHTML_CANDIDATE and advertises only StructuredNotifications/HostHtml
for that test image; ordinary builds keep the provider capability mask at zero.

.PARAMETER SourceCache
Preserved, read-only CMake cache used only to seed feature/dependency choices.
Compiler/link flags, SDK RC and target-port switches come from the discovered
W10M toolchain; the source cache itself is never configured or written.

.PARAMETER SkipNative
Skip the native engine phase (discovery still runs and is recorded).

.PARAMETER Resume
Resume a prior orchestration root after a failed/interrupted phase. Requires
its recorded discovery and configured native build; existing completed CX
phase identities are reused, while incomplete phase directories are rejected.

.PARAMETER SkipComponent
Skip the CX component phase.

.PARAMETER SkipApps
Skip the CX app phase.

.PARAMETER DryRun
Print every resolved command with its explicit versions without executing.
Creates no directories and runs no builds.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools/w10m/build/build-m10-clean.ps1 -DryRun

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools/w10m/build/build-m10-clean.ps1 -OutRoot "$env:TEMP\opencode\m10-clean-01" -SdkVersion 10.0.19041.0 -SkipSign

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools/w10m/build/build-m10-clean.ps1 -OutRoot "$env:TEMP\opencode\m10-clean-02" -SdkVersion 10.0.19041.0 -NativeParallel 2 -SkipSign
#>
#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$OutRoot = (Join-Path $env:TEMP 'opencode\m10-clean'),
    [string]$VsPath,
    [string]$MsvcVersion,
    [string]$SdkRoot,
    [string]$SdkVersion = '10.0.19041.0',
    [string]$FrameworkRef,
    [string]$SignThumbprint,
    [string]$SourceCache,
    [switch]$SkipSign,
    [string[]]$NativeTargets = @('WebCore', 'JavaScriptCore'),
    [ValidateRange(1, 64)][int]$NativeParallel = 8,
    [switch]$HostHtmlCandidate,
    [switch]$Resume,
    [switch]$SkipNative,
    [switch]$SkipComponent,
    [switch]$SkipApps,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$buildDir = $PSScriptRoot
$discoverScript = Join-Path $buildDir 'discover-arm32-toolchain.ps1'
$prepareCacheScript = Join-Path $buildDir 'prepare-arm32-engine-cache.ps1'
$envScript = Join-Path $repo 'Tools\w10m\arm32-w10m-env.ps1'
$componentScript = Join-Path $buildDir 'build-arm32-cx-component.ps1'
$appScript = Join-Path $buildDir 'build-arm32-cx-compiled-probe.ps1'
$toolchainFile = Join-Path $repo 'WebKitLibraries\toolchains\Toolchain-ARM32-W10M-clang.cmake'

foreach ($required in @($discoverScript, $prepareCacheScript, $envScript, $componentScript, $appScript, $toolchainFile)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required orchestration input missing: $required" }
}
if (-not $SkipSign -and -not $SignThumbprint) { $SkipSign = $true }
if ($SkipSign -and $SignThumbprint) { throw 'Specify either -SignThumbprint or -SkipSign, not both' }
if (-not $NativeTargets -and -not $SkipNative) { throw 'No native targets selected; specify -NativeTargets or -SkipNative' }

$OutRoot = [IO.Path]::GetFullPath($OutRoot)
if ((Test-Path -LiteralPath $OutRoot) -and -not $DryRun -and -not $Resume) {
    throw "Use a fresh -OutRoot for each clean build: $OutRoot already exists"
}
if ($Resume -and -not (Test-Path -LiteralPath $OutRoot)) {
    throw "Cannot resume a missing orchestration root: $OutRoot"
}

$commands = New-Object System.Collections.Generic.List[string]
function Add-Command([string]$command) { $commands.Add($command) }

# Resolve the VS installation once so every child receives an explicit -VsPath.
# Same recipe as the component build: first vswhere instance with an ARM32 cl.
if (-not $VsPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere not found: $vswhere; specify -VsPath" }
    foreach ($instance in (& $vswhere -all -products '*' -format json | ConvertFrom-Json)) {
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
if (-not $MsvcVersion) { throw "No ARM32 toolset under $VsPath; specify -MsvcVersion" }
if (-not $SdkRoot) { $SdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
if (-not $SdkRoot) { throw 'No Windows Kits root; specify -SdkRoot' }

$nativeDir = Join-Path $OutRoot 'native'
$nativeCache = Join-Path $OutRoot 'native-initial-cache.cmake'
$componentDir = Join-Path $OutRoot 'component'
$appsDir = Join-Path $OutRoot 'apps'

if ($Resume -and -not $DryRun) {
    foreach ($requiredState in @((Join-Path $OutRoot 'discovery.json'), (Join-Path $nativeDir 'CMakeCache.txt'))) {
        if (-not (Test-Path -LiteralPath $requiredState)) { throw "Resume state is incomplete: $requiredState" }
    }
}

"Resolved build identity:"
"  VsPath      : $VsPath"
"  MsvcVersion : $MsvcVersion"
"  SdkRoot     : $SdkRoot"
"  SdkVersion  : $SdkVersion"
"  OutRoot     : $OutRoot"
"  Native      : $($NativeTargets -join ', ') x $NativeParallel jobs"
''

# Phase 0: discovery gate. Blockers fail before any compilation.
$discoverArgs = @('-SdkVersion', $SdkVersion, '-VsPath', $VsPath, '-MsvcVersion', $MsvcVersion, '-Json')
if (-not $Resume) { Add-Command "powershell -ExecutionPolicy Bypass -File $discoverScript $($discoverArgs -join ' ')" }
$discovery = $null
if (-not $DryRun) {
    if ($Resume) {
        $discovery = Get-Content -LiteralPath (Join-Path $OutRoot 'discovery.json') -Raw | ConvertFrom-Json
        if ($discovery.Blockers -gt 0) { throw "Recorded discovery has $($discovery.Blockers) blocker(s); refusing to resume" }
        if ($discovery.SelectedToolset -ne $MsvcVersion -or $discovery.SdkVersionOverride -ne $SdkVersion) {
            throw 'Resume toolset/SDK differs from the recorded discovery; use the original explicit versions'
        }
        if ($discovery.VsPathOverride -and $discovery.VsPathOverride -ne $VsPath) {
            throw 'Resume Visual Studio path differs from the recorded discovery'
        }
        $cacheText = Get-Content -LiteralPath (Join-Path $nativeDir 'CMakeCache.txt') -Raw
        if ($cacheText -notmatch '(?m)^PORT:STRING=Win\r?$' -or $cacheText -notmatch '(?m)^WK_W10M:BOOL=ON\r?$') {
            throw 'Resume native CMake cache is not configured for the W10M port'
        }
        $expectedCandidate = if ($HostHtmlCandidate) { 'ON' } else { 'OFF' }
        if ($cacheText -notmatch "(?m)^WKW10M_HOSTHTML_CANDIDATE:BOOL=$expectedCandidate\r?$") {
            throw "Resume HostHtml candidate setting differs from the CMake cache (expected $expectedCandidate)"
        }
    } else {
        New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null
        $discoveryJson = & $discoverScript -SdkVersion $SdkVersion -VsPath $VsPath -MsvcVersion $MsvcVersion -Json
        if ($LASTEXITCODE -ne 0) { throw "Toolchain discovery reported blockers (exit $LASTEXITCODE); resolve them before building" }
        $discoveryJson | Set-Content (Join-Path $OutRoot 'discovery.json')
        $discovery = $discoveryJson | ConvertFrom-Json
    }
    if ($discovery.Blockers -gt 0) { throw "Toolchain discovery recorded $($discovery.Blockers) blocker(s); refusing to build" }
    "Discovery PASS: $($discovery.Findings.Count) capabilities measured, 0 blockers"
    ''
}

# Phase 1: native engine clean configure + eight-job named targets.
if (-not $SkipNative) {
    if (-not $Resume) {
        if (-not $SourceCache) { $SourceCache = Join-Path $repo 'build-arm-uwp-skia\CMakeCache.txt' }
        if (-not (Test-Path -LiteralPath $SourceCache)) { throw "Feature baseline cache is missing: $SourceCache; specify -SourceCache or retain a known-good feature cache" }
        Add-Command "powershell -ExecutionPolicy Bypass -File $prepareCacheScript -SourceCache $SourceCache -OutFile $nativeCache"
        $candidateOption = if ($HostHtmlCandidate) { ' -DWKW10M_HOSTHTML_CANDIDATE=ON' } else { '' }
        $configureCmd = "cmake -S $repo -B $nativeDir -G Ninja -C $nativeCache -DPORT=Win -DWK_W10M=ON$candidateOption -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
        Add-Command ". $envScript -VsPath $VsPath -MsvcVersion $MsvcVersion -SdkRoot $SdkRoot -SdkVersion $SdkVersion"
        Add-Command $configureCmd
    } else {
        Add-Command ". $envScript -VsPath $VsPath -MsvcVersion $MsvcVersion -SdkRoot $SdkRoot -SdkVersion $SdkVersion"
    }
    foreach ($target in $NativeTargets) { Add-Command "cmake --build $nativeDir --target $target --parallel $NativeParallel" }
    if (-not $DryRun) {
        "--- native engine ($(if ($Resume) { 'resume' } else { 'clean configure' }), $NativeParallel-job named targets) ---"
        . $envScript -VsPath $VsPath -MsvcVersion $MsvcVersion -SdkRoot $SdkRoot -SdkVersion $SdkVersion
        if (-not $Resume) {
            if (Test-Path -LiteralPath $nativeDir) { throw "Native build directory already exists; use a fresh -OutRoot: $nativeDir" }
            & $prepareCacheScript -SourceCache $SourceCache -OutFile $nativeCache
            if ($LASTEXITCODE -ne 0) { throw 'Could not prepare clean native feature/dependency cache' }
            $candidateArgs = if ($HostHtmlCandidate) { @('-DWKW10M_HOSTHTML_CANDIDATE=ON') } else { @() }
            & cmake -S $repo -B $nativeDir -G Ninja -C $nativeCache -DPORT=Win -DWK_W10M=ON @candidateArgs -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" 2>&1 |
                Tee-Object -FilePath (Join-Path $OutRoot 'native-configure.log')
            if ($LASTEXITCODE -ne 0) { throw 'Native clean configure failed' }
        }
        foreach ($target in $NativeTargets) {
            $logName = if ($Resume) { "native-build-$target-resume.log" } else { "native-build-$target.log" }
            & cmake --build $nativeDir --target $target --parallel $NativeParallel 2>&1 |
                Tee-Object -FilePath (Join-Path $OutRoot $logName)
            if ($LASTEXITCODE -ne 0) { throw "Native target failed: $target" }
        }
        "    native targets built with $NativeParallel jobs: $($NativeTargets -join ', ')"
    }
}

# Phase 2: CX component with explicit versions into a fresh directory.
if (-not $SkipComponent) {
    $componentIdentity = Join-Path $componentDir 'build-identity.json'
    $reuseComponent = $Resume -and (Test-Path -LiteralPath $componentIdentity)
    if ($Resume -and (Test-Path -LiteralPath $componentDir) -and -not $reuseComponent) { throw "Incomplete component output cannot be resumed: $componentDir" }
    if (-not $reuseComponent) { Add-Command "powershell -ExecutionPolicy Bypass -File $componentScript -OutDir $componentDir -VsPath $VsPath -MsvcVersion $MsvcVersion -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Architecture arm" }
    if (-not $DryRun) {
        if ($reuseComponent) { '--- CX component (reusing recorded successful identity) ---' }
        else {
            '--- CX component (clean, explicit versions) ---'
            & $componentScript -OutDir $componentDir -VsPath $VsPath -MsvcVersion $MsvcVersion -SdkRoot $SdkRoot -SdkVersion $SdkVersion -Architecture arm
            if ($LASTEXITCODE -ne 0) { throw "CX component build failed (exit $LASTEXITCODE)" }
        }
    }
}

# Phase 3: CX app with explicit versions into a fresh directory.
if (-not $SkipApps) {
    $appArgs = @('-OutDir', $appsDir, '-VsPath', $VsPath, '-MsvcVersion', $MsvcVersion, '-SdkRoot', $SdkRoot, '-SdkVersion', $SdkVersion)
    if ($FrameworkRef) { $appArgs += @('-FrameworkRef', $FrameworkRef) }
    if ($SkipSign) { $appArgs += @('-SkipSign') } else { $appArgs += @('-SignThumbprint', $SignThumbprint) }
    $appIdentity = Join-Path $appsDir 'build-identity.json'
    $reuseApps = $Resume -and (Test-Path -LiteralPath $appIdentity)
    if ($Resume -and (Test-Path -LiteralPath $appsDir) -and -not $reuseApps) { throw "Incomplete app output cannot be resumed: $appsDir" }
    if (-not $reuseApps) { Add-Command "powershell -ExecutionPolicy Bypass -File $appScript $($appArgs -join ' ')" }
    if (-not $DryRun) {
        if ($reuseApps) { '--- CX app (reusing recorded successful identity) ---' }
        else {
            '--- CX app (clean, explicit versions) ---'
            $invokeArgs = @{
                OutDir = $appsDir; VsPath = $VsPath; MsvcVersion = $MsvcVersion
                SdkRoot = $SdkRoot; SdkVersion = $SdkVersion
            }
            if ($FrameworkRef) { $invokeArgs['FrameworkRef'] = $FrameworkRef }
            if ($SkipSign) { $invokeArgs['SkipSign'] = $true } else { $invokeArgs['SignThumbprint'] = $SignThumbprint }
            & $appScript @invokeArgs
            if ($LASTEXITCODE -ne 0) { throw "CX app build failed (exit $LASTEXITCODE)" }
        }
    }
}

if ($DryRun) {
    ''
    'Dry run: resolved commands with explicit versions (no directories created, no builds executed):'
    foreach ($command in $commands) { "  DRYRUN: $command" }
    ''
    'Dry run complete.'
    exit 0
}

# Phase 4: aggregate build identity manifest (discovery + per-phase identities).
$sourceCommit = (& git -C $repo rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $sourceCommit = 'unknown' } else { $sourceCommit = $sourceCommit.Trim() }
$sourceChanges = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($path in @(& git -C $repo diff --name-only HEAD 2>$null)) { if ($path) { [void]$sourceChanges.Add($path) } }
foreach ($path in @(& git -C $repo ls-files --others --exclude-standard 2>$null)) { if ($path) { [void]$sourceChanges.Add($path) } }
$sourceStateLines = [System.Collections.Generic.List[string]]::new()
foreach ($path in $sourceChanges) {
    $fullPath = Join-Path $repo $path
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
        $fileHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
    } else {
        $fileHash = 'DELETED'
    }
    $sourceStateLines.Add("$path`t$fileHash")
}
$sourceStateBytes = [Text.Encoding]::UTF8.GetBytes(($sourceStateLines -join "`n"))
$sourceChangesSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($sourceStateBytes))
function Get-PhaseIdentity([string]$path) {
    if (Test-Path -LiteralPath $path) { return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json) }
    return $null
}
$identity = [ordered]@{
    GeneratedBy   = 'Tools/w10m/build/build-m10-clean.ps1'
    Resumed       = [bool]$Resume
    SourceCommit  = $sourceCommit
    SourceDirty   = [bool]$sourceChanges.Count
    SourceChangesSha256 = $sourceChangesSha256
    SourceChangedPaths = @($sourceChanges)
    VsPath        = $VsPath
    MsvcVersion   = $MsvcVersion
    SdkRoot       = $SdkRoot
    SdkVersion    = $SdkVersion
    NativeTargets = $NativeTargets
    NativeJobs    = $NativeParallel
    HostHtmlCandidate = [bool]$HostHtmlCandidate
    Discovery     = $discovery
    Component     = (Get-PhaseIdentity (Join-Path $componentDir 'build-identity.json'))
    App           = (Get-PhaseIdentity (Join-Path $appsDir 'build-identity.json'))
    Commands      = $commands
}
$identity | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutRoot 'm10-build-identity.json')
'M10 clean-build orchestration PASS; identity: ' + (Join-Path $OutRoot 'm10-build-identity.json')
