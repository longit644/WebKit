<#
.SYNOPSIS
    Proves the frozen WKUWP V1 ABI has one layout for both boundary compilers.

.DESCRIPTION
    Compiles Tools/uwp/build/wkuwp-abi-layout-check.c for ARM32 with the two
    compilers that meet at the boundary:

      native  - the clang-cl that WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake
                uses for the engine, targeting the triple named in that same file.
      cx      - the MSVC ARM32 cl that builds the /ZW C++/CX component.

    Every assertion in that translation unit restates a row of the structure table
    in artifact/native-abi.md, so a compiler disagreement on any size, offset,
    enumeration width or calling convention fails here before a producer or a
    consumer is written against the wrong layout. The check is compile-only and
    links nothing.

    Each compiler compiles the check twice, as C and as C++, because the boundary
    has a C++ half: the framework consumer is C++/CX. That second pass is not
    redundant. Writing the first provider against the headers failed to compile as
    C++ because a structure tag was forward declared and then defined as an
    anonymous typedef, which C accepts as two unrelated types and C++ rejects; only
    a C++ pass could have caught it before an implementation depended on it.

    This script resolves the native compiler and triple by reading the toolchain
    file rather than duplicating them, so the check cannot drift from the real
    build. It fails rather than guessing when a path is missing.

.EXAMPLE
    .\check-wkuwp-abi-layout.ps1
    .\check-wkuwp-abi-layout.ps1 -MsvcVersion 14.42.35207 -Verbose
#>
[CmdletBinding()]
param(
    # Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.
    [string]$MsvcVersion,

    # Directory receiving the two throwaway objects.
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\wkuwp-abi-layout')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/uwp/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:CheckFile = Join-Path $PSScriptRoot 'wkuwp-abi-layout-check.c'
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\uwp'
$script:ToolchainFile = Join-Path $script:RepoRoot 'WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022'

function Get-ToolchainSetting {
    # Reads one `set(NAME value)` definition out of the CMake toolchain file so
    # this check and the real engine build cannot name different compilers.
    param(
        [Parameter(Mandatory = $true)][string]$Name
    )

    # The value may be a quoted path containing spaces, or a bare word such as a
    # triple, so capture the rest of the line lazily and strip quotes afterwards.
    $match = Select-String -Path $script:ToolchainFile -Pattern "^\s*set\($Name\s+(.+?)\s*\)?\s*$" |
        Select-Object -First 1
    if (-not $match) {
        throw "Toolchain setting $Name not found in $($script:ToolchainFile)"
    }
    return $match.Matches[0].Groups[1].Value -replace '^"|"$', ''
}

function Get-ArmSdk {
    # Newest Windows SDK that can still build this target, returned as its version
    # directory name so callers can reach both the header and library trees.
    #
    # The header trees are architecture neutral: um, ucrt, shared and winrt hold
    # one copy of each header. Only the import libraries are per-architecture, so
    # the deciding test is Lib\<version>\um\arm together with Lib\<version>\ucrt\arm.
    # A newer SDK without those library folders cannot link ARM32 and is skipped
    # even though it parses the same headers.
    $versionRoot = Join-Path $script:WindowsKitsRoot 'Lib'
    if (-not (Test-Path -LiteralPath $versionRoot)) {
        throw "Windows SDK library root not found: $versionRoot"
    }

    $candidates = Get-ChildItem -LiteralPath $versionRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'um\arm')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\arm')) -and
            (Test-Path -LiteralPath (Join-Path $script:WindowsKitsRoot "Include\$($_.Name)\ucrt"))
        } |
        Sort-Object -Property Name -Descending
    if (-not $candidates) {
        throw "No Windows SDK with ARM32 import libraries found under $versionRoot"
    }
    return $candidates[0].Name
}

function Get-ArmToolset {
    $toolsetRoot = Join-Path $script:VsRoot 'BuildTools\VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $toolsetRoot)) {
        throw "MSVC toolset root not found: $toolsetRoot"
    }

    $candidates = Get-ChildItem -LiteralPath $toolsetRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm\cl.exe') }
    if (-not $candidates) {
        throw "No MSVC toolset with an ARM32 cl.exe found under $toolsetRoot"
    }

    if ($MsvcVersion) {
        $requested = $candidates | Where-Object { $_.Name -eq $MsvcVersion }
        if (-not $requested) {
            throw "Requested toolset $MsvcVersion is not installed; available: $(($candidates.Name) -join ', ')"
        }
        return $requested[0].FullName
    }

    return ($candidates | Sort-Object -Property Name -Descending | Select-Object -First 1).FullName
}

function Invoke-LayoutCheck {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ObjectPath,
        [Parameter(Mandatory = $true)][hashtable]$Environment
    )

    Write-Host "--- $Label ---"
    $previous = @{}
    foreach ($name in $Environment.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $Environment[$name], 'Process')
    }

    try {
        Write-Host "    $Exe $($Arguments -join ' ')"
        $output = & $Exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        foreach ($name in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }

    if ($output) {
        $output | ForEach-Object { Write-Host "    $_" }
    }

    if ($exitCode -ne 0) {
        throw "$Label layout check failed with exit code $exitCode"
    }

    if (-not (Test-Path -LiteralPath $ObjectPath)) {
        throw "$Label reported success but produced no object at $ObjectPath"
    }
    Write-Host "    PASS"
}

if (-not (Test-Path -LiteralPath $script:CheckFile)) {
    throw "Layout check source not found: $script:CheckFile"
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$sdkVersion = Get-ArmSdk
$toolsetDir = Get-ArmToolset
$triple = Get-ToolchainSetting -Name 'WK_UWP_TRIPLE'
if (-not $env:WK_UWP_CLANG_CL) {
    . (Join-Path $script:RepoRoot 'Tools\uwp\arm32-uwp-env.ps1') -MsvcVersion $MsvcVersion -SdkVersion $sdkVersion
}
$clangCl = $env:WK_UWP_CLANG_CL

if (-not (Test-Path -LiteralPath $clangCl)) {
    throw "Native clang-cl named by the toolchain file is missing: $clangCl"
}

$sdkInclude = Join-Path $script:WindowsKitsRoot "Include\$sdkVersion"
$ucrtInclude = Join-Path $sdkInclude 'ucrt'
$umInclude = Join-Path $sdkInclude 'um'
$sharedInclude = Join-Path $sdkInclude 'shared'
$winrtInclude = Join-Path $sdkInclude 'winrt'
$toolsetInclude = Join-Path $toolsetDir 'include'

Write-Host 'ABI layout conformance for armv7'
Write-Host "  native compiler : $clangCl"
Write-Host "  target triple    : $triple"
Write-Host "  cx compiler      : $(Join-Path $toolsetDir 'bin\Hostx64\arm\cl.exe')"
Write-Host "  msvc toolset     : $toolsetDir"
Write-Host "  sdk version      : $sdkVersion"
Write-Host "  objects          : $OutDir"

# Native side: mirrors the toolchain file's compiler, triple and header search
# path, and additionally compiles as C11 so the assertions are portable.
#
# Each argument is parenthesized because PowerShell's comma operator binds tighter
# than '+', so an unparenthesized '/Fo' + $path inside this array literal would
# silently concatenate every following element into one argument.
Invoke-LayoutCheck -Label 'native (clang-cl, armv7-unknown-windows-msvc, C)' -Exe $clangCl `
    -ObjectPath (Join-Path $OutDir 'wkuwp-abi-layout-native.obj') `
    -Arguments @(
    ('--target=' + $triple), '/nologo', '/c', '/W4', '/TC', '/std:c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/imsvc' + $toolsetInclude), ('/imsvc' + $ucrtInclude), ('/imsvc' + $umInclude),
    ('/imsvc' + $sharedInclude), ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-abi-layout-native.obj')), $script:CheckFile
    ) -Environment @{}

Invoke-LayoutCheck -Label 'native (clang-cl, armv7-unknown-windows-msvc, C++)' -Exe $clangCl `
    -ObjectPath (Join-Path $OutDir 'wkuwp-abi-layout-native-cxx.obj') `
    -Arguments @(
    ('--target=' + $triple), '/nologo', '/c', '/W4', '/TP',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/imsvc' + $toolsetInclude), ('/imsvc' + $ucrtInclude), ('/imsvc' + $umInclude),
    ('/imsvc' + $sharedInclude), ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-abi-layout-native-cxx.obj')), $script:CheckFile
    ) -Environment @{}

# C++/CX side: the ARM32 MSVC compiler that owns /ZW, given the same headers.
# The /ZW flag is deliberately absent because the assertions are plain C and must
# hold whether or not C++/CX is enabled for the translation unit under test.
Invoke-LayoutCheck -Label 'cx (MSVC ARM32 cl, C)' `
    -Exe (Join-Path $toolsetDir 'bin\Hostx64\arm\cl.exe') `
    -ObjectPath (Join-Path $OutDir 'wkuwp-abi-layout-cx.obj') `
    -Arguments @(
    '/nologo', '/c', '/TC', '/W4', '/std:c11',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-abi-layout-cx.obj')), $script:CheckFile
    ) -Environment @{
    INCLUDE = @(
        $toolsetInclude, $ucrtInclude, $umInclude, $sharedInclude, $winrtInclude
    ) -join ';'
}

Invoke-LayoutCheck -Label 'cx (MSVC ARM32 cl, C++)' `
    -Exe (Join-Path $toolsetDir 'bin\Hostx64\arm\cl.exe') `
    -ObjectPath (Join-Path $OutDir 'wkuwp-abi-layout-cx-cxx.obj') `
    -Arguments @(
    '/nologo', '/c', '/TP', '/W4', '/EHsc',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-abi-layout-cx-cxx.obj')), $script:CheckFile
    ) -Environment @{
    INCLUDE = @(
        $toolsetInclude, $ucrtInclude, $umInclude, $sharedInclude, $winrtInclude
    ) -join ';'
}

Write-Host ''
Write-Host 'Both boundary compilers agree on every frozen size, offset,'
Write-Host 'enumeration value and calling convention, as C and as C++.'

# The assertions are only meaningful if they were evaluated under the ARM ABI, so
# the produced objects are inspected rather than assumed from the command line.
$llvmBin = Split-Path -Parent $clangCl
$readObj = Join-Path $llvmBin 'llvm-readobj.exe'
if (Test-Path -LiteralPath $readObj) {
    Write-Host ''
    Write-Host 'ARM32 object inspection'
    foreach ($object in @('wkuwp-abi-layout-native.obj', 'wkuwp-abi-layout-native-cxx.obj',
            'wkuwp-abi-layout-cx.obj', 'wkuwp-abi-layout-cx-cxx.obj')) {
        $objectPath = Join-Path $OutDir $object
        if (-not (Test-Path -LiteralPath $objectPath)) {
            throw "Expected object missing: $objectPath"
        }
        $headers = (& $readObj '--file-headers' $objectPath 2>&1) -join "`n"
        if ($headers -notmatch 'IMAGE_FILE_MACHINE_ARMNT') {
            throw "$object is not ARMNT:`n$headers"
        }
        Write-Host "    $object COFF-ARM / IMAGE_FILE_MACHINE_ARMNT"
    }
}
else {
    Write-Host ''
    Write-Host "Skipped ARM32 object inspection: llvm-readobj.exe not found beside $clangCl"
}

exit 0
