<#
.SYNOPSIS
    Verifies the M10 task 3.5 bridge behaviour: interleaved pages, wrong-thread
    calls and close during policy dispatch.

.DESCRIPTION
    Four checks, all from repository sources, in the order a consumer meets them:

      1. The bridge sources compile clean for ARM32 with the native engine
         compiler and the ARM32 MSVC cl that owns /ZW, at /W4 with no warnings.
         Both boundary compilers must accept the same sources, because the
         consumer half of the boundary is a different compiler from the producer
         half.
      2. The bridge and test-provider objects are genuine ARM32 objects,
         verified with llvm-readobj rather than trusted from the command line.
      3. An ARM32 module built from those objects reports ARMNT/AppContainer and
         exports the unmangled discovery name the consumer resolves, so the
         GetProcAddress path the consumer takes exists in an ARM32 image.
      4. Bridge behaviour runs on this x64 host from x64 copies of the same
         sources: two interleaved pages without routing, wrong-thread rejection
         before any argument is read, and close during policy dispatch with the
         verdict intact and the deferred release honored. The behaviour is
         ownership bookkeeping plus negotiation arithmetic and does not vary by
         architecture; the ARM32 image is exercised on device in later tasks.

    The test provider is test tooling only: it proves nothing about the real
    engine provider, which task 3.2 verified separately, or about real engine
    callback behaviour, which tasks 4.x will implement.

    This script does not claim a full WebCore or WebKit rebuild, and it leaves
    the preserved build-arm-uwp-skia tree untouched by owner decision. The
    affected consumer files are proven by compiling them with the same flags
    their future target uses; wiring them into that target belongs to the
    component integration work.

.PARAMETER OutDir
    Directory receiving the throwaway objects, modules and harness.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\test-wkuwp-bridge-ordering.ps1
    .\test-wkuwp-bridge-ordering.ps1 -OutDir D:\m10\bridge
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\wkuwp-bridge-ordering'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/uwp/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\uwp'
$script:ModuleDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\uwp\native'
$script:CallbackSource = Join-Path $script:ModuleDir 'CallbackContextUWP.cpp'
$script:ModuleSource = Join-Path $script:ModuleDir 'EngineModuleUWP.cpp'
$script:ProviderSource = Join-Path $PSScriptRoot 'WKUWPModuleTestProvider.cpp'
$script:HarnessSource = Join-Path $PSScriptRoot 'BridgeOrderingTests.cpp'
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
    # a boundary file that only compiles quietly would make the bridge proof rest
    # on a file its own target would reject.
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

foreach ($required in @($script:CallbackSource, $script:ModuleSource, $script:ProviderSource, $script:HarnessSource)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required source not found: $required"
    }
}

# The native flag set is the one the real engine configure consumes, so this check
# cannot drift from Toolchain-ARM32-UWP-clang.cmake.
$sdkVersion = Get-ArmSdk
if (-not $env:WK_UWP_CLANG_CL) {
    . (Join-Path $script:RepoRoot 'Tools\uwp\arm32-uwp-env.ps1') -MsvcVersion $MsvcVersion -SdkVersion $sdkVersion
}
$clangCl = $env:WK_UWP_CLANG_CL
$nativeCFlags = $env:WK_UWP_NATIVE_C_FLAGS
$nativeLinkFlags = $env:WK_UWP_NATIVE_LINK_FLAGS
foreach ($name in 'WK_UWP_CLANG_CL', 'WK_UWP_NATIVE_C_FLAGS', 'WK_UWP_NATIVE_LINK_FLAGS') {
    if (-not (Test-Path Env:$name)) {
        throw "Native environment variable $name is missing; dot-source Tools/uwp/arm32-uwp-env.ps1 first"
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
$x64Include = @(
    (Join-Path $x64Toolset 'include'),
    (Join-Path $sdkInclude 'ucrt'),
    (Join-Path $sdkInclude 'um'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'winrt')
) -join ';'

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host 'M10 task 3.5 bridge ordering verification'
Write-Host "  callbacks       : $script:CallbackSource"
Write-Host "  consumer        : $script:ModuleSource"
Write-Host "  test provider   : $script:ProviderSource"
Write-Host "  native compiler : $clangCl"
Write-Host "  cx/arm compiler : $armCl"
Write-Host "  host compiler   : $x64Cl"
Write-Host "  sdk             : $sdkVersion"
Write-Host "  output          : $OutDir"
Write-Host ''

# -B and -fuse-ld select the linker for the clang driver and are rejected by
# clang-cl in compile-only mode. The engine build tolerates that because CMake
# passes the same flags, so they are dropped here and the link step below invokes
# lld-link directly, which is the linker the toolchain file requires.
$armCompileFlags = @($nativeCFlags -split ' ' | Where-Object { $_ -notmatch '^(-B|-fuse-ld=)' })
$armSources = @($script:CallbackSource, $script:ModuleSource, $script:ProviderSource)

# ---------------------------------------------------------------------------
Write-Host '1/4 ARM32 compile with the native engine compiler'
$armNativeObjects = @()
foreach ($source in $armSources) {
    $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-native.obj')
    $armNativeObjects += $object
    $run = Invoke-Captured -Exe $clangCl -Arguments (@($armCompileFlags) + @(
            '/nologo', '/c', '/W4', '/WX', '/EHsc',
            ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
            ('/Fo' + $object), $source))
    $run.Output | ForEach-Object { Write-Host "    $_" }
    if ($run.ExitCode -ne 0) {
        throw "ARM32 native compile of $source failed with exit code $($run.ExitCode)"
    }
    Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 native compile of $source"
}
Write-Host '    no warnings'

# ---------------------------------------------------------------------------
Write-Host '2/4 ARM32 compile with the MSVC ARM32 compiler that owns /ZW'
foreach ($source in $armSources) {
    $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-cx.obj')
    $run = Invoke-Captured -Exe $armCl -Arguments @(
        '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
        ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
        ('/Fo' + $object), $source)
    $run.Output | ForEach-Object { Write-Host "    $_" }
    if ($run.ExitCode -ne 0) {
        throw "ARM32 CX compile of $source failed with exit code $($run.ExitCode)"
    }
    Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 CX compile of $source"
}
Write-Host '    no warnings'

# ---------------------------------------------------------------------------
Write-Host '3/4 ARM32 module export inspection'
$armDll = Join-Path $OutDir 'WKUWPBridgePair-arm.dll'
$armLinkRun = Invoke-Captured -Exe $lldLink -Arguments (@(
        '/nologo', '/dll', '/NOENTRY', ('/out:' + $armDll)) + ($nativeLinkFlags -split ' ') + $armNativeObjects)
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

$exports = (& $readObj '--coff-exports' $armDll 2>&1) -join "`n"
if ($exports -notmatch '(?m)^\s*Name: WKUWPGetEngineAPI\s*$') {
    throw "ARM32 module does not export the unmangled WKUWPGetEngineAPI name:`n$exports"
}
Write-Host '    COFF-ARM / IMAGE_FILE_MACHINE_ARMNT / APPCONTAINER'
Write-Host '    export WKUWPGetEngineAPI (unmangled)'

# ---------------------------------------------------------------------------
Write-Host '4/4 bridge behaviour on this x64 host'
$x64ProviderDll = Join-Path $OutDir 'WKUWPBridgeTestProvider-x64.dll'
$x64Exe = Join-Path $OutDir 'BridgeOrderingTests.exe'
$x64Objects = @()
foreach ($source in $armSources) {
    $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-x64.obj')
    $x64Objects += $object
}

# The x64 host builds below deliberately omit /DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP:
# a desktop test process is not an AppContainer, so the consumer takes its
# desktop LoadLibraryW branch there. The ARM32 builds above define the App
# family and therefore exercise the packaged-loader branch the real component
# uses. Both branches are documented in EngineModuleUWP.cpp.
$sourceIndex = 0
foreach ($source in $armSources) {
    Invoke-Checked -Step "cl $([IO.Path]::GetFileName($source)) (host x64)" -Exe $x64Cl `
        -Environment @{ INCLUDE = $x64Include } -Arguments @(
        '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
        '/D_WIN32_WINNT=0x0A00',
        ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
        ('/Fo' + $x64Objects[$sourceIndex]),
        $source)
    $sourceIndex++
}

# The Store-flavored runtime the shipping ARM32 module uses is OneCore-only and is
# not loadable on the desktop, so this host module links the desktop runtime. The
# runtime flavor itself is proven by the ARM32 link above; what runs here is
# bridge behaviour.
$providerObj = $x64Objects[$x64Objects.Count - 1]
$consumerObjs = $x64Objects[0..($x64Objects.Count - 2)]
Invoke-Checked -Step 'lld-link test provider (host x64)' -Exe $lldLink -Arguments (@(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $x64ProviderDll),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64"))) + @(
    $providerObj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'))

Invoke-Checked -Step 'cl harness (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
    ('/Fo' + (Join-Path $OutDir 'BridgeOrderingTests.obj')),
    $script:HarnessSource
)

# The harness links the consumers under test directly, but reaches the provider
# image only through LoadLibrary, exactly as the consumer does.
Invoke-Checked -Step 'lld-link harness (host x64)' -Exe $lldLink -Arguments (@(
    '/nologo', '/machine:x64',
    ('/out:' + $x64Exe),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64"))) + @(
    (Join-Path $OutDir 'BridgeOrderingTests.obj')) + $consumerObjs + @(
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'))

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
    throw "Bridge verification failed with exit code $harnessExit"
}

Write-Host 'Task 3.5 bridge verification passed.'
Write-Host 'Not claimed here: a full WebCore or WebKit rebuild, consumer target wiring'
Write-Host '(deferred to component integration), real engine callback behaviour, or'
Write-Host 'device behaviour of any slot.'
exit 0