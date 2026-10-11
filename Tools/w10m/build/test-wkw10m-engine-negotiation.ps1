<#
.SYNOPSIS
    Verifies the M10 task 3.2 WKW10M discovery export, capability mask and build identity.

.DESCRIPTION
    Four checks, all from repository sources, in the order a consumer meets them:

      1. The provider compiles clean for ARM32 with the native engine compiler and
         the ARM32 MSVC cl that owns /ZW, at /W4 with no warnings. Both boundary
         compilers must accept the same source, because the consumer half of the
         boundary is a different compiler from the producer half.
      2. The provider compiles to a genuine ARM32 object, verified with
         llvm-readobj rather than trusted from the command line.
      3. An ARM32 module built from that object exports the unmangled discovery
         name, so GetProcAddress("WKW10MGetEngineAPI") - what a C++/CX consumer does
         after LoadLibrary - can succeed against the shipping engine module.
      4. Negotiation behaviour runs on this x64 host from an x64 copy of the same
         provider source, resolved through LoadLibrary/GetProcAddress exactly as a
         consumer resolves it. The behaviour is pure negotiation arithmetic and
         does not vary by architecture; the ARM32 image is exercised on device in
         later tasks.

    This script does not claim a full WebCore rebuild. The affected engine target
    is proven by compiling the new translation unit with the same flags the target
    uses; adding it to the preserved build-arm-uwp-skia tree is left to the native
    rebuild/orchestration tasks.

.PARAMETER OutDir
    Directory receiving the throwaway objects, modules and harness.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\test-wkw10m-engine-negotiation.ps1
    .\test-wkw10m-engine-negotiation.ps1 -OutDir D:\m10\abi
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\w10m-engine-negotiation'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/w10m/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\w10m'
$script:ProviderSource = Join-Path $script:RepoRoot 'Source\WebCore\platform\w10m\WKW10MEngineAPI.cpp'
$script:HarnessSource = Join-Path $PSScriptRoot 'WKW10MEngineAPITests.cpp'
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
    # a boundary file that only compiles quietly would make the ABI unproven.
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

foreach ($required in @($script:ProviderSource, $script:HarnessSource)) {
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

Write-Host 'M10 task 3.2 WKW10M discovery verification'
Write-Host "  provider        : $script:ProviderSource"
Write-Host "  native compiler : $clangCl"
Write-Host "  cx/arm compiler : $armCl"
Write-Host "  host compiler   : $x64Cl"
Write-Host "  sdk             : $sdkVersion"
Write-Host "  output          : $OutDir"
Write-Host ''

# ---------------------------------------------------------------------------
Write-Host '1/4 ARM32 compile with the native engine compiler'
# -B and -fuse-ld select the linker for the clang driver and are rejected by
# clang-cl in compile-only mode. The engine build tolerates that because CMake
# passes the same flags, so they are dropped here and the link step below invokes
# lld-link directly, which is the linker the toolchain file requires.
$armNativeCompileFlags = @($nativeCFlags -split ' ' | Where-Object { $_ -notmatch '^(-B|-fuse-ld=)' })
$armNativeObj = Join-Path $OutDir 'WKW10MEngineAPI-arm-native.obj'
$armNativeRun = Invoke-Captured -Exe $clangCl -Arguments (@($armNativeCompileFlags) + @(
        '/nologo', '/c', '/W4', '/WX',
        ('/I' + $script:AbiHeaderDir),
        ('/Fo' + $armNativeObj), $script:ProviderSource))
$armNativeOutput = $armNativeRun.Output
$armNativeOutput | ForEach-Object { Write-Host "    $_" }
if ($armNativeRun.ExitCode -ne 0) {
    throw "ARM32 native compile failed with exit code $($armNativeRun.ExitCode)"
}
Assert-NoCompilerDiagnostic -Output $armNativeOutput -Step 'ARM32 native compile'
Write-Host '    no warnings'

# ---------------------------------------------------------------------------
Write-Host '2/4 ARM32 compile with the MSVC ARM32 compiler that owns /ZW'
$armCxObj = Join-Path $OutDir 'WKW10MEngineAPI-arm-cx.obj'
$armCxRun = Invoke-Captured -Exe $armCl -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + $armCxObj), $script:ProviderSource)
$armCxOutput = $armCxRun.Output
$armCxOutput | ForEach-Object { Write-Host "    $_" }
if ($armCxRun.ExitCode -ne 0) {
    throw "ARM32 CX compile failed with exit code $($armCxRun.ExitCode)"
}
Assert-NoCompilerDiagnostic -Output $armCxOutput -Step 'ARM32 CX compile'
Write-Host '    no warnings'

# ---------------------------------------------------------------------------
Write-Host '3/4 ARM32 module export inspection'
$armDll = Join-Path $OutDir 'WKW10MEngineAPI-arm.dll'
$armLinkRun = Invoke-Captured -Exe $lldLink -Arguments (@(
        '/nologo', '/dll', '/NOENTRY', ('/out:' + $armDll)) + ($nativeLinkFlags -split ' ') + @($armNativeObj))
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
Write-Host '4/4 negotiation behaviour on this x64 host'
$x64Dll = Join-Path $OutDir 'WKW10MEngineAPI-x64.dll'
$x64Exe = Join-Path $OutDir 'WKW10MEngineAPITests.exe'
$x64Obj = Join-Path $OutDir 'WKW10MEngineAPI-x64.obj'
$harnessObj = Join-Path $OutDir 'WKW10MEngineAPITests.obj'

Invoke-Checked -Step 'cl provider (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + $x64Obj),
    $script:ProviderSource
)

# The Store-flavored runtime the shipping ARM32 module uses is OneCore-only and is
# not loadable on the desktop, so this host module links the desktop runtime. The
# runtime flavor itself is proven by build-arm32-cx-probe.ps1 and the ARM32 link
# above; what runs here is negotiation behaviour.
Invoke-Checked -Step 'lld-link provider module (host x64)' -Exe $lldLink -Arguments @(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $x64Dll),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")),
    $x64Obj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'
)

Invoke-Checked -Step 'cl harness (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + $harnessObj),
    $script:HarnessSource
)

# The harness never links against the provider, so it can only reach the entry
# point through GetProcAddress on the loaded module.
Invoke-Checked -Step 'lld-link harness (host x64)' -Exe $lldLink -Arguments @(
    '/nologo', '/machine:x64',
    ('/out:' + $x64Exe),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64")),
    $harnessObj,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'
)

Write-Host ''
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $x64Exe $x64Dll
    $harnessExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
Write-Host ''

if ($harnessExit -ne 0) {
    throw "Negotiation verification failed with exit code $harnessExit"
}

Write-Host 'Task 3.2 discovery verification passed.'
Write-Host 'Not claimed here: a full WebCore rebuild, the EngineModuleUWP consumer,'
Write-Host 'module pinning (task 3.3) or device behaviour of any engine table slot.'
exit 0
