<#
.SYNOPSIS
    Verifies the M10 task 3.6 legacy export surface: frozen smoke semantics and
    negotiated-table-only new consumers.

.DESCRIPTION
    Three checks, none of which modifies the preserved engine tree:

      1. The explicit map in Source/WebCore/platform/uwp/LegacyExportMapUWP.h
         compiles clean for ARM32 with both boundary compilers, as C++ on each.
         Only C++ is exercised because every consumer of this surface - the
         wrapper, MiniBrowser and the diagnostic hosts - is C++, and the legacy
         header itself uses C++-only declarations. The check translation unit
         assigns each of the 23 declared exports to its recorded signature
         typedef, so any drift fails here instead of silently changing what the
         smoke consumers call.
      2. The preserved build-arm-uwp-skia/bin/WebCore.dll still exports all 24
         legacy names unmangled, inspected read-only with llvm-readobj. The
         tree itself is never configured, built or written.
      3. New M10 consumers resolve nothing but the negotiated table: every
         GetProcAddress literal in the new native/ sources must be
         "WKUWPGetEngineAPI", test files may additionally resolve the
         WKUWPTestProvider* harness exports and the two component activation
         exports the task 2.4 lifetime harness resolves by design, and no
         legacy WK*UWP identifier may appear in the new consumer sources at
         all. The map header and its check file are the map, not consumers,
         and are excluded by name.
      4. Frozen PageUWP.h and the diagnostic application sources remain
         byte-identical to HEAD. Engine restructuring and task-6.1 wrapper/
         presenter extraction have explicit independent build/behavior gates.

.PARAMETER OutDir
    Directory receiving the throwaway objects.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\check-wkuwp-legacy-surface.ps1
    .\check-wkuwp-legacy-surface.ps1 -OutDir D:\m10\legacy
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\wkuwp-legacy-surface'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/uwp/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:LegacyHeaderDir = Join-Path $script:RepoRoot 'Source\WebCore\platform\uwp'
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\uwp'
$script:CheckFile = Join-Path $PSScriptRoot 'wkuwp-legacy-surface-check.c'
$script:ToolchainFile = Join-Path $script:RepoRoot 'WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake'
$script:PreservedEngine = Join-Path $script:RepoRoot 'build-arm-uwp-skia\bin\WebCore.dll'
$script:ConsumerDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\uwp\native'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

# Every legacy export the map records, in wrapper-bind order plus the three the
# wrapper never binds. The preserved module must export each one unmangled.
$script:LegacyExports = @(
    'WKCreatePageUWP', 'WKDestroyPageUWP', 'WKLoadHTMLUWP', 'WKNavigatePageUWP',
    'WKGoBackPageUWP', 'WKGoForwardPageUWP', 'WKReloadPageUWP', 'WKStopPageUWP',
    'WKSetNavigationPolicyHandlerPageUWP', 'WKResizePageUWP', 'WKScrollPageUWP',
    'WKPointerPageUWP', 'WKTickPageUWP', 'WKPaintPageUWP', 'WKPaintPageScaledUWP',
    'WKPageNeedsPaintUWP', 'WKGetPageStateUWP', 'WKEnableCompositorUWP',
    'WKDisableCompositorUWP', 'WKRenderCompositorUWP', 'WKInstallClipboardDispatcherUWP',
    'WKInitializeEngineUWP', 'WKGetPageDocumentStateUWP', 'WKRenderFirstPageUWP'
)

# Smoke surface this slice must leave byte-identical to prove retained semantics.
# Engine restructuring (4.1/4.2), wrapper presenter/metrics extraction (6.1) and
# the MiniBrowser visible-bounds shell evolution (6.3) intentionally change their
# files by design. The frozen engine declarations and diagnostic application
# sources remain byte-identical; presenter/input ownership is checked by the
# rendering/input contract tests plus the MiniBrowser ARM build and device logs.
$script:SmokeSurface = @(
    'Source/WebCore/platform/uwp/PageUWP.h',
    'Tools/uwp/RuntimeSmoke/SmokeHost.cpp',
    'Tools/uwp/FpuProbe/FpuProbe.cpp',
    'Tools/uwp/TlsSmoke/TlsSmoke.cpp'
)

function Get-ToolchainSetting {
    # Reads one `set(NAME value)` definition out of the CMake toolchain file so
    # this check and the real engine build cannot name different compilers.
    param(
        [Parameter(Mandatory = $true)][string]$Name
    )

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
    $toolsetRoot = Join-Path $script:VsRoot 'VC\Tools\MSVC'
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
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = & $Exe @Arguments 2>&1
            $exitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previousPreference
        }
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
    throw "Legacy surface check source not found: $script:CheckFile"
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

Write-Host 'M10 task 3.6 legacy export surface verification'
Write-Host "  map               : Source/WebCore/platform/uwp/LegacyExportMapUWP.h"
Write-Host "  native compiler   : $clangCl"
Write-Host "  cx compiler       : $(Join-Path $toolsetDir 'bin\Hostx64\arm\cl.exe')"
Write-Host "  preserved engine  : $script:PreservedEngine"
Write-Host "  objects           : $OutDir"
Write-Host ''

# Every consumer of the legacy surface is C++, so the freeze is proven in C++
# on both compilers. The /WX flag is deliberately present: a warning here would
# mean the frozen surface no longer compiles quietly.
Invoke-LayoutCheck -Label 'native (clang-cl, armv7-unknown-windows-msvc, C++)' -Exe $clangCl `
    -ObjectPath (Join-Path $OutDir 'wkuwp-legacy-surface-native.obj') `
    -Arguments @(
    ('--target=' + $triple), '/nologo', '/c', '/W4', '/WX', '/TP', '/EHsc',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/imsvc' + $toolsetInclude), ('/imsvc' + $ucrtInclude), ('/imsvc' + $umInclude),
    ('/imsvc' + $sharedInclude), ('/I' + $script:LegacyHeaderDir), ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-legacy-surface-native.obj')), $script:CheckFile
    ) -Environment @{}

Invoke-LayoutCheck -Label 'cx (MSVC ARM32 cl, C++)' `
    -Exe (Join-Path $toolsetDir 'bin\Hostx64\arm\cl.exe') `
    -ObjectPath (Join-Path $OutDir 'wkuwp-legacy-surface-cx.obj') `
    -Arguments @(
    '/nologo', '/c', '/TP', '/W4', '/WX', '/EHsc',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:LegacyHeaderDir), ('/I' + $script:AbiHeaderDir),
    ('/Fo' + (Join-Path $OutDir 'wkuwp-legacy-surface-cx.obj')), $script:CheckFile
    ) -Environment @{
    INCLUDE = @(
        $toolsetInclude, $ucrtInclude, $umInclude, $sharedInclude, $winrtInclude
    ) -join ';'
}

Write-Host ''
Write-Host '--- preserved engine exports (read-only inspection) ---'
if (-not (Test-Path -LiteralPath $script:PreservedEngine)) {
    throw "Preserved engine module not found: $script:PreservedEngine"
}
$llvmBin = Split-Path -Parent $clangCl
$readObj = Join-Path $llvmBin 'llvm-readobj.exe'
$exports = (& $readObj '--coff-exports' $script:PreservedEngine 2>&1) -join "`n"
$missing = @()
foreach ($name in $script:LegacyExports) {
    if ($exports -match "(?m)^\s*Name: $([regex]::Escape($name))\s*$") {
        Write-Host "    export $name (unmangled)"
    }
    else {
        $missing += $name
    }
}
if ($missing.Count -gt 0) {
    throw "Preserved engine is missing legacy exports: $($missing -join ', ')"
}
Write-Host "    all $($script:LegacyExports.Count) legacy exports present"

Write-Host ''
Write-Host '--- new consumers resolve only the negotiated table ---'
$offenders = @()
$consumerFiles = @(Get-ChildItem -LiteralPath $script:ConsumerDir -Filter '*.cpp') +
    @(Get-ChildItem -LiteralPath $script:ConsumerDir -Filter '*.h') |
    Where-Object { $_ }
foreach ($file in $consumerFiles) {
    $text = [IO.File]::ReadAllText($file.FullName)
    foreach ($match in [regex]::Matches($text, 'GetProcAddress\s*\(\s*\w+\s*,\s*"([^"]+)"')) {
        if ($match.Groups[1].Value -ne 'WKUWPGetEngineAPI') {
            $offenders += "$($file.Name): $($match.Groups[1].Value)"
        }
    }
    foreach ($match in [regex]::Matches($text, '\bWK[A-Z][A-Za-z]*UWP\b')) {
        $offenders += "$($file.Name): legacy identifier $($match.Value)"
    }
}
foreach ($file in @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*Tests.cpp') | Where-Object { $_ }) {
    $text = [IO.File]::ReadAllText($file.FullName)
    foreach ($match in [regex]::Matches($text, 'GetProcAddress\s*\(\s*\w+\s*,\s*"([^"]+)"')) {
        $resolved = $match.Groups[1].Value
        # The task 2.4 lifetime harness resolves the component activation
        # exports by design; everything else test-side is the engine discovery
        # export or a test-provider harness export.
        $componentActivation = $resolved -eq 'DllGetActivationFactory' -or $resolved -eq 'DllCanUnloadNow'
        if ($resolved -ne 'WKUWPGetEngineAPI' -and -not $resolved.StartsWith('WKUWPTestProvider') -and -not $componentActivation) {
            $offenders += "$($file.Name): $($match.Groups[1].Value)"
        }
    }
}
if ($offenders.Count -gt 0) {
    throw "New consumers resolve more than the negotiated table:`n$($offenders -join "`n")"
}
Write-Host '    only WKUWPGetEngineAPI (plus test-provider harness exports) is resolved'

Write-Host ''
Write-Host '--- smoke surface unchanged in this slice ---'
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    Push-Location $script:RepoRoot
    try {
        & git diff --quiet HEAD -- $script:SmokeSurface 2>&1 | Out-Null
        $smokeExit = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    $ErrorActionPreference = $previousPreference
}
if ($smokeExit -ne 0) {
    throw "The smoke surface differs from the committed HEAD; semantics cannot be claimed retained"
}
Write-Host '    PageUWP declarations and diagnostic hosts: identical to HEAD'

Write-Host ''
Write-Host 'Task 3.6 legacy surface verification passed.'
Write-Host 'Not claimed here: callable per-export forwards (they need engine state from'
Write-Host 'tasks 4.x), a rebuilt engine, or device behaviour of any export.'
exit 0
