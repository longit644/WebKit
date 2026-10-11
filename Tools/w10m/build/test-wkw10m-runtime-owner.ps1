<#
.SYNOPSIS
    Verifies the M10 task 7.5 runtime owner: cleanup tickets, failed-enqueue
    accounting, quiescence, dispatcher loss, hook revocation, engine pinning
    and close/recreate counts.

.DESCRIPTION
    Seven checks, all from repository sources, in the order a consumer meets them:

      1. The neutral owner, service, route and module sources compile clean for
         ARM32 with the native engine compiler at /W4 with no warnings, using
         the toolchain flags exactly: the real engine build disables C++
         unwinding (/EHs-c-), these files are written for that model, and
         appending /EHsc after /EHs-c- trips an LLVM 19.1.5 ARM backend crash
         (cleanupret selection) on the clipboard translation units.
      2. The same sources compile clean for ARM32 with the MSVC ARM32 compiler
         that owns /ZW, at /W4 with no warnings and without /ZW. Any ref class,
         hat type or Windows:: projection reference would fail here, so this is
         the neutrality proof for every translation unit in the stack.
      3. The owner header carries no CX/WinRT token and no engine header reaches
         for it, extending the task 7.1 neutrality proof to the runtime owner.
      4. The owner stack objects link into a genuine ARM32 AppContainer module,
         verified with llvm-readobj rather than trusted from the command line.
      5. Owner behaviour runs on this x64 host from x64 copies of the same
         sources against the test-double engine provider, reached only through
         the loader exactly as the owner reaches it: ticketed copy/cut/paste
         with acknowledged completion, engine-facing table with working postWake,
         hook registration and revocation, close/recreate generations with
         carried totals, shutdown with acknowledged tickets and revoked hooks,
         fail-closed post-shutdown behavior, dispatcher-loss drops with
         synchronous close release, foreign-thread rejection, engine bind/pin,
         shutdown-result independence and pin survival past the owner. The
         behaviour is ownership bookkeeping plus HRESULT arithmetic and does not
         vary by architecture; WinRT behaviour belongs to the device suite.
      6. The task 7.2 routing gate still passes unchanged: no shared source is
         modified by this task except as noted, so the routing proof is a
         regression by construction and is rerun to confirm it.
      7. G4 stays open: this task proves the 7.5 runtime owner, while task 6.3
         resize/recovery remains the only other open G4 item. No combined
         presentation claim is made here.

    This script does not claim a full WebCore or WebKit rebuild, component
    target wiring beyond the diagnostic consumer (the production control adopts
    the owner in section 8), WinRT behaviour, or device behaviour of any slot.

.PARAMETER OutDir
    Directory receiving the throwaway objects, modules and harness.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\test-wkuwp-runtime-owner.ps1
    .\test-wkuwp-runtime-owner.ps1 -OutDir D:\m10\runtime-owner
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\wkuwp-runtime-owner'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/w10m/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\w10m'
$script:PlatformDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\w10m\platform'
$script:NativeDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\w10m\native'
$script:OwnerSource = Join-Path $script:PlatformDir 'RuntimeOwnerW10M.cpp'
$script:OwnerHeader = Join-Path $script:PlatformDir 'RuntimeOwnerUWP.h'
$script:ServiceSource = Join-Path $script:PlatformDir 'ClipboardServiceW10M.cpp'
$script:RouteSource = Join-Path $script:PlatformDir 'ClipboardRouteW10M.cpp'
$script:ModuleSource = Join-Path $script:NativeDir 'EngineModuleW10M.cpp'
$script:ProviderSource = Join-Path $PSScriptRoot 'WKUWPModuleTestProvider.cpp'
$script:HarnessSource = Join-Path $PSScriptRoot 'RuntimeOwnerTests.cpp'
$script:RouteGate = Join-Path $PSScriptRoot 'test-wkuwp-clipboard-route.ps1'
$script:EngineDir = Join-Path $script:RepoRoot 'Source\WebCore\platform\w10m'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

function ConvertTo-QuotedArgument {
    # PowerShell does not quote arguments for native commands, so a path under
    # "Program Files (x86)" would otherwise arrive split at the space.
    param([Parameter(Mandatory = $true)][string]$Argument)

    if ($Argument -notmatch '[\s()]') {
        return $Argument
    }
    if ($Argument -match '^(/[A-Za-z]+)(.+)$') {
        return $Matches[1] + '"' + $Matches[2] + '"'
    }
    return '"' + $Argument + '"'
}

function Invoke-Captured {
    # Runs a compiler without letting a stderr banner become a terminating error,
    # and returns its output and exit code so the caller can report both.
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $invocation = @($Arguments | ForEach-Object { ConvertTo-QuotedArgument -Argument $_ })
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Exe @invocation 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    return [pscustomobject]@{ Output = @($output | ForEach-Object { "$_" }); ExitCode = $exitCode }
}

function Invoke-Checked {
    # Runs a compiler or linker and fails on its own exit code rather than on
    # anything written to stderr, which SDK tools use for banners.
    param(
        [Parameter(Mandatory = $true)][string]$Step,
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [hashtable]$Environment = @{}
    )

    Write-Host "  $Step"
    $invocation = @($Arguments | ForEach-Object { ConvertTo-QuotedArgument -Argument $_ })

    $previous = @{}
    foreach ($name in $Environment.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Exe @invocation 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
        foreach ($name in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }

    foreach ($line in $output) {
        Write-Host "    $line"
    }
    if ($exitCode -ne 0) {
        throw "$Step failed with exit code $exitCode"
    }
}

function Assert-NoCompilerDiagnostic {
    # The engine builds at /W4, so a warning here is a failure: silently shipping
    # an owner file that only compiles quietly would make the 7.5 proof rest on
    # a file its own target would reject.
    param(
        # A clean compile produces no output at all, which is the success case.
        [AllowEmptyCollection()][string[]]$Output,
        [Parameter(Mandatory = $true)][string]$Step
    )

    $diagnostics = @($Output | Where-Object { "$_" -match ': (error|warning) [A-Z]+[0-9]+' })
    if ($diagnostics.Count -gt 0) {
        throw "$Step produced compiler diagnostics:`n$($diagnostics -join "`n")"
    }
}

function Get-ArmSdk {
    # Newest SDK with ARM32 import libraries; only those can link this target.
    $versionRoot = Join-Path $script:WindowsKitsRoot 'Lib'
    $candidates = Get-ChildItem -LiteralPath $versionRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'um\arm')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\arm'))
        } |
        Sort-Object -Property Name -Descending
    if (-not $candidates) {
        throw "No Windows SDK with ARM32 import libraries found under $versionRoot"
    }
    return $candidates[0].Name
}

function Get-Toolset {
    # $HostArchitecture is the cl.exe host directory, for example 'arm' or 'x64'.
    param([Parameter(Mandatory = $true)][string]$Parent, [Parameter(Mandatory = $true)][string]$HostArchitecture)

    $matches = Get-ChildItem -LiteralPath $Parent -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "bin\Hostx64\$HostArchitecture\cl.exe") } |
        Sort-Object -Property Name -Descending
    if (-not $matches) {
        throw "No MSVC toolset with a host $HostArchitecture cl.exe under $Parent"
    }

    if ($MsvcVersion) {
        $requested = $matches | Where-Object { $_.Name -eq $MsvcVersion }
        if (-not $requested) {
            throw "Requested toolset $MsvcVersion is not installed; available: $(($matches.Name) -join ', ')"
        }
        return $requested[0].FullName
    }
    return $matches[0].FullName
}

foreach ($required in @($script:OwnerSource, $script:OwnerHeader, $script:ServiceSource, $script:RouteSource, $script:ModuleSource, $script:ProviderSource, $script:HarnessSource, $script:RouteGate)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required source not found: $required"
    }
}

# The native flag set is the one the real engine configure consumes, so this check
# cannot drift from Toolchain-ARM32-W10M-clang.cmake.
$sdkVersion = Get-ArmSdk
if (-not $env:WK_W10M_CLANG_CL) {
    . (Join-Path $script:RepoRoot 'Tools\w10m\arm32-w10m-env.ps1') -MsvcVersion $MsvcVersion -SdkVersion $sdkVersion
}
$clangCl = $env:WK_W10M_CLANG_CL
$nativeCFlags = $env:WK_W10M_NATIVE_C_FLAGS
$nativeLinkFlags = $env:WK_W10M_NATIVE_LINK_FLAGS
foreach ($name in 'WK_W10M_CLANG_CL', 'WK_W10M_NATIVE_C_FLAGS', 'WK_W10M_NATIVE_LINK_FLAGS') {
    if (-not (Test-Path Env:$name)) {
        throw "Native environment variable $name is missing; dot-source Tools/w10m/arm32-w10m-env.ps1 first"
    }
}

$llvmBin = Split-Path -Parent $clangCl
$readObj = Join-Path $llvmBin 'llvm-readobj.exe'
$lldLink = Join-Path $llvmBin 'lld-link.exe'

$armToolset = Get-Toolset -Parent (Join-Path $script:VsRoot 'VC\Tools\MSVC') -HostArchitecture arm
$x64Toolset = Get-Toolset -Parent (Join-Path $script:VsRoot 'VC\Tools\MSVC') -HostArchitecture x64
$armCl = Join-Path $armToolset 'bin\Hostx64\arm\cl.exe'
$x64Cl = Join-Path $x64Toolset 'bin\Hostx64\x64\cl.exe'

$sdkInclude = Join-Path $script:WindowsKitsRoot "Include\$sdkVersion"
$armInclude = "$armToolset\include;$sdkInclude\ucrt;$sdkInclude\um;$sdkInclude\shared;$sdkInclude\winrt"
$x64Include = @(
    (Join-Path $x64Toolset 'include'),
    (Join-Path $sdkInclude 'ucrt'),
    (Join-Path $sdkInclude 'um'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'winrt')
) -join ';'
$x64LibPaths = @(
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")))
$x64RuntimeLibs = @(
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib')

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host 'M10 task 7.5 runtime owner verification'
Write-Host "  owner           : $script:OwnerSource"
Write-Host "  native compiler : $clangCl"
Write-Host "  cx/arm compiler : $armCl"
Write-Host "  host compiler   : $x64Cl"
Write-Host "  sdk             : $sdkVersion"
Write-Host "  output          : $OutDir"
Write-Host ''

$armSources = @($script:OwnerSource, $script:ServiceSource, $script:RouteSource, $script:ModuleSource)
$includeFlags = @(('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir))

$previousInclude = $env:INCLUDE
$env:INCLUDE = $armInclude
try {
    # ---------------------------------------------------------------------------
    Write-Host '1/7 ARM32 compile of the neutral owner stack with the native engine compiler'
    $armCompileFlags = @($nativeCFlags -split ' ' | Where-Object { $_ -notmatch '^(-B|-fuse-ld=)' })
    $armObjects = @()
    foreach ($source in $armSources) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-native.obj')
        $armObjects += $object
        $run = Invoke-Captured -Exe $clangCl -Arguments (@($armCompileFlags) + @(
                '/nologo', '/c', '/W4', '/WX') + $includeFlags + @(
                ('/Fo' + $object), $source))
        $run.Output | ForEach-Object { Write-Host "    $_" }
        if ($run.ExitCode -ne 0) {
            throw "ARM32 native compile of $source failed with exit code $($run.ExitCode)"
        }
        Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 native compile of $source"
    }
    Write-Host '    no warnings'

    # ---------------------------------------------------------------------------
    Write-Host '2/7 ARM32 compile of the neutral owner stack with the MSVC ARM32 compiler'
    foreach ($source in $armSources) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-cx.obj')
        $run = Invoke-Captured -Exe $armCl -Arguments (@(
            '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
            '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00') + $includeFlags + @(
            ('/Fo' + $object), $source))
        $run.Output | ForEach-Object { Write-Host "    $_" }
        if ($run.ExitCode -ne 0) {
            throw "ARM32 CX compile of $source failed with exit code $($run.ExitCode)"
        }
        Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 CX compile of $source"
    }
    Write-Host '    no warnings (neutral: no /ZW needed)'
}
finally {
    $env:INCLUDE = $previousInclude
}

# ---------------------------------------------------------------------------
Write-Host '3/7 owner header neutrality audit'
$projectionHits = Select-String -LiteralPath $script:OwnerHeader -Pattern @('ref class', 'ref struct', 'Windows::', 'CoreDispatcher', 'String^', '#using') -SimpleMatch
if ($projectionHits) {
    throw "Neutral owner header carries CX/WinRT tokens:`n$($projectionHits -join "`n")"
}
Write-Host '    neutral owner header carries no CX/WinRT token'

$engineHits = Get-ChildItem -LiteralPath $script:EngineDir -Filter '*.h' |
    Select-String -Pattern 'RuntimeOwnerUWP\.h'
if ($engineHits) {
    throw "Engine header reaches for the new owner header:`n$($engineHits -join "`n")"
}
Write-Host '    no engine header includes the owner header'

# ---------------------------------------------------------------------------
Write-Host '4/7 ARM32 module inspection'
$armDll = Join-Path $OutDir 'WKUWPRuntimeOwner-arm.dll'
$armLinkRun = Invoke-Captured -Exe $lldLink -Arguments (@(
        '/nologo', '/dll', '/NOENTRY', ('/out:' + $armDll)) + ($nativeLinkFlags -split ' ') + $armObjects)
$armLinkRun.Output | ForEach-Object { Write-Host "    $_" }
if ($armLinkRun.ExitCode -ne 0) {
    throw "ARM32 module link failed with exit code $($armLinkRun.ExitCode)"
}
if (-not (Test-Path -LiteralPath $armDll)) {
    throw "ARM32 module link reported success but produced no $armDll"
}

$headers = (& $readObj '--file-headers' $armDll 2>&1) -join "`n"
if ($headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT') {
    throw "ARM32 module is not ARMNT:`n$headers"
}
if ($headers -notmatch 'IMAGE_DLL_CHARACTERISTICS_APPCONTAINER') {
    throw "ARM32 module is not an AppContainer image:`n$headers"
}
Write-Host '    COFF-ARM / IMAGE_FILE_MACHINE_ARMNT / APPCONTAINER'

# ---------------------------------------------------------------------------
Write-Host '5/7 runtime owner behaviour on this x64 host'
$x64Exe = Join-Path $OutDir 'RuntimeOwnerTests.exe'
$x64ProviderDll = Join-Path $OutDir 'WKUWPOwnerTestProvider-x64.dll'
$x64Objects = @()
foreach ($source in ($armSources + @($script:ProviderSource))) {
    $x64Objects += Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-x64.obj')
}

# The x64 host builds below deliberately omit /DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP:
# a desktop test process is not an AppContainer, so the module takes its desktop
# LoadLibraryW branch there. The ARM32 builds above define the App family and
# therefore exercise the packaged-loader branch the real component uses.
$sourceIndex = 0
foreach ($source in ($armSources + @($script:ProviderSource))) {
    Invoke-Checked -Step "cl $([IO.Path]::GetFileName($source)) (host x64)" -Exe $x64Cl `
        -Environment @{ INCLUDE = $x64Include } -Arguments (@(
        '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
        '/D_WIN32_WINNT=0x0A00') + $includeFlags + @(
        ('/Fo' + $x64Objects[$sourceIndex]),
        $source))
    $sourceIndex++
}

# The Store-flavored runtime the shipping ARM32 module uses is OneCore-only and is
# not loadable on the desktop, so this host module links the desktop runtime. The
# runtime flavor itself is proven by the ARM32 link above; what runs here is
# owner behaviour.
$providerObj = $x64Objects[$x64Objects.Count - 1]
$ownerObjs = $x64Objects[0..($x64Objects.Count - 2)]
Invoke-Checked -Step 'lld-link test provider (host x64)' -Exe $lldLink -Arguments (@(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $x64ProviderDll)) + $x64LibPaths + @($providerObj) + $x64RuntimeLibs)

Invoke-Checked -Step 'cl harness (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
    ('/Fo' + (Join-Path $OutDir 'RuntimeOwnerTests.obj')),
    $script:HarnessSource
)

# The harness links the owner stack directly, but reaches the provider image
# only through LoadLibrary, exactly as the owner does.
Invoke-Checked -Step 'lld-link harness (host x64)' -Exe $lldLink -Arguments (@(
    '/nologo', '/machine:x64',
    ('/out:' + $x64Exe)) + $x64LibPaths + @(
    (Join-Path $OutDir 'RuntimeOwnerTests.obj')) + $ownerObjs + $x64RuntimeLibs)

Write-Host ''
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $x64Exe $x64ProviderDll
    $harnessExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
Write-Host ''

if ($harnessExit -ne 0) {
    throw "Runtime owner verification failed with exit code $harnessExit"
}

# ---------------------------------------------------------------------------
Write-Host '6/7 task 7.2 routing regression'
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $script:RouteGate -OutDir (Join-Path $OutDir 'route-regression')
    $routeExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}

if ($routeExit -ne 0) {
    throw "Task 7.2 routing regression failed with exit code $routeExit"
}

# ---------------------------------------------------------------------------
Write-Host '7/7 G4 gate status'
Write-Host '    7.5 runtime owner proven above; task 6.3 resize/recovery stays open,'
Write-Host '    so no combined presentation claim is made here.'

Write-Host 'Task 7.5 runtime owner verification passed.'
Write-Host 'Not claimed here: a full WebCore or WebKit rebuild, production control'
Write-Host 'adoption of the owner (section 8), WinRT behaviour, device behaviour of'
Write-Host 'any slot, or combined G4 presentation acceptance.'
exit 0
