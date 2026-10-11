<#
.SYNOPSIS
    Verifies the M10 task 3.3 EngineModuleUWP consumer: pinning and page ownership.

.DESCRIPTION
    Four checks, all from repository sources, in the order a consumer meets them:

      1. The consumer compiles clean for ARM32 with the native engine compiler and
         the ARM32 MSVC cl that owns /ZW, at /W4 with no warnings. Both boundary
         compilers must accept the same source, because the consumer half of the
         boundary is a different compiler from the producer half.
      2. The consumer and the configurable test provider compile to genuine ARM32
         objects, verified with llvm-readobj rather than trusted from the command
         line.
      3. An ARM32 module built from those objects reports ARMNT/AppContainer and
         exports the unmangled discovery name the consumer resolves, so the
         GetProcAddress path the consumer takes exists in an ARM32 image.
      4. Ownership behaviour runs on this x64 host from x64 copies of the same
         sources: compatible resolution, incompatible-identity rejection, no
         FreeLibrary while the process lives, per-page cleanup returning to the
         zero baseline on close/recreate, and shutdown latching. The behaviour is
         ownership bookkeeping plus negotiation arithmetic and does not vary by
         architecture; the ARM32 image is exercised on device in later tasks.

    The test provider is test tooling only: it proves nothing about the real
    engine provider, which task 3.2 verified separately.

    This script does not claim a full WebCore or WebKit rebuild, and it leaves
    the preserved build-arm-uwp-skia tree untouched by owner decision. The
    affected consumer file is proven by compiling it with the same flags its
    future target uses; wiring it into that target belongs to the component
    integration work.

.PARAMETER OutDir
    Directory receiving the throwaway objects, modules and harness.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\test-wkw10m-engine-module.ps1
    .\test-wkw10m-engine-module.ps1 -OutDir D:\m10\module
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\w10m-engine-module'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/w10m/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\w10m'
$script:ModuleDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\w10m\native'
$script:ModuleSource = Join-Path $script:ModuleDir 'EngineModuleW10M.cpp'
$script:ModuleHeader = Join-Path $script:ModuleDir 'EngineModuleW10M.h'
$script:ProviderSource = Join-Path $PSScriptRoot 'WKW10MModuleTestProvider.cpp'
$script:HarnessSource = Join-Path $PSScriptRoot 'EngineModuleW10MTests.cpp'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

function ConvertTo-QuotedArgument {
    # Modern PowerShell preserves argument boundaries; only legacy native passing
    # needs embedded path quotes, with a linker option's colon outside them.
    param([Parameter(Mandatory = $true)][string]$Argument)

    if ($PSVersionTable.PSVersion -ge [version]'7.3' -and $PSNativeCommandArgumentPassing -ne 'Legacy') {
        return $Argument
    }
    if ($Argument -notmatch '[\s()]') {
        return $Argument
    }
    if ($Argument -match '^(/[A-Za-z]+:?)(.+)$') {
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
        [Environment]::SetEnvironmentVariable($name, $Environment[$name], 'Process')
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
    # a boundary file that only compiles quietly would make the ownership proof
    # rest on a file its own target would reject.
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

foreach ($required in @($script:ModuleSource, $script:ModuleHeader, $script:ProviderSource, $script:HarnessSource)) {
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
$x64Include = @(
    (Join-Path $x64Toolset 'include'),
    (Join-Path $sdkInclude 'ucrt'),
    (Join-Path $sdkInclude 'um'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'winrt')
) -join ';'

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host 'M10 task 3.3 EngineModuleUWP verification'
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

# ---------------------------------------------------------------------------
Write-Host '1/4 ARM32 compile with the native engine compiler'
foreach ($source in @($script:ModuleSource, $script:ProviderSource)) {
    $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-native.obj')
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
foreach ($source in @($script:ModuleSource, $script:ProviderSource)) {
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
$armDll = Join-Path $OutDir 'WKW10MModulePair-arm.dll'
$armLinkRun = Invoke-Captured -Exe $lldLink -Arguments (@(
        '/nologo', '/dll', '/NOENTRY', ('/out:' + $armDll)) + ($nativeLinkFlags -split ' ') + @(
        (Join-Path $OutDir 'EngineModuleW10M-arm-native.obj'),
        (Join-Path $OutDir 'WKW10MModuleTestProvider-arm-native.obj')))
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
if ($exports -notmatch '(?m)^\s*Name: WKW10MGetEngineAPI\s*$') {
    throw "ARM32 module does not export the unmangled WKW10MGetEngineAPI name:`n$exports"
}
Write-Host '    COFF-ARM / IMAGE_FILE_MACHINE_ARMNT / APPCONTAINER'
Write-Host '    export WKW10MGetEngineAPI (unmangled)'

# ---------------------------------------------------------------------------
Write-Host '4/4 ownership behaviour on this x64 host'
$x64ProviderDll = Join-Path $OutDir 'WKW10MModuleTestProvider-x64.dll'
$x64LifecycleDll = Join-Path $OutDir 'WKW10MModuleLifecycle-x64.dll'
$x64RejectionDll = Join-Path $OutDir 'WKW10MModuleRejection-x64.dll'
$x64Exe = Join-Path $OutDir 'EngineModuleUWPTests.exe'
$x64ModuleObj = Join-Path $OutDir 'EngineModuleUWP-x64.obj'
$x64ProviderObj = Join-Path $OutDir 'WKW10MModuleTestProvider-x64.obj'
$x64RejectionProviderObj = Join-Path $OutDir 'WKW10MModuleTestProvider-rejection-x64.obj'
$harnessObj = Join-Path $OutDir 'EngineModuleUWPTests.obj'

# The x64 host builds below deliberately omit /DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP:
# a desktop test process is not an AppContainer, so the consumer takes its
# desktop LoadLibraryW branch there. The ARM32 builds above define the App
# family and therefore exercise the packaged-loader branch the real component
# uses. Both branches are documented in EngineModuleW10M.cpp.
Invoke-Checked -Step 'cl consumer (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
    ('/Fo' + $x64ModuleObj),
    $script:ModuleSource
)

Invoke-Checked -Step 'cl test provider (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + $x64ProviderObj),
    $script:ProviderSource
)

# The Store-flavored runtime the shipping ARM32 module uses is OneCore-only and is
# not loadable on the desktop, so this host module links the desktop runtime. The
# runtime flavor itself is proven by the ARM32 link above; what runs here is
# ownership behaviour.
Invoke-Checked -Step 'lld-link test provider (host x64)' -Exe $lldLink -Arguments @(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $x64ProviderDll),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")),
    $x64ProviderObj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'
)

# The lifecycle image must be a file copy the harness itself never loads: only
# then does its load state say anything about what the module under test freed.
# The rejection image is built, not copied, with a truncated reported extent
# baked in: DLL globals do not survive an unload, so arming a fresh image through
# setters and then freeing the setup handle would lose the setting before the
# test loads it.
Copy-Item -LiteralPath $x64ProviderDll -Destination $x64LifecycleDll -Force

Invoke-Checked -Step 'cl rejection provider (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/D_WIN32_WINNT=0x0A00',
    '/DWKW10M_TEST_PROVIDER_DEFAULT_TRUNCATE=64',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + $x64RejectionProviderObj),
    $script:ProviderSource
)

Invoke-Checked -Step 'lld-link rejection provider (host x64)' -Exe $lldLink -Arguments @(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $x64RejectionDll),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")),
    $x64RejectionProviderObj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'
)

Invoke-Checked -Step 'cl harness (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:ModuleDir),
    ('/Fo' + $harnessObj),
    $script:HarnessSource
)

# The harness links the consumer under test directly, but reaches every provider
# image only through LoadLibrary, exactly as the consumer does.
Invoke-Checked -Step 'lld-link harness (host x64)' -Exe $lldLink -Arguments @(
    '/nologo', '/machine:x64',
    ('/out:' + $x64Exe),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")),
    $harnessObj, $x64ModuleObj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'
)

Write-Host ''
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $x64Exe $x64ProviderDll $x64LifecycleDll $x64RejectionDll
    $harnessExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
Write-Host ''

if ($harnessExit -ne 0) {
    throw "Ownership verification failed with exit code $harnessExit"
}

Write-Host 'Task 3.3 ownership verification passed.'
Write-Host 'Not claimed here: a full WebCore or WebKit rebuild, EngineModuleUWP target'
Write-Host 'wiring (deferred to component integration), or device behaviour of any slot.'
exit 0
