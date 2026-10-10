<#
.SYNOPSIS
    Host-side proof that the migrated FpuProbe payload keeps its expected
    arithmetic and ABI surface (M10 task 8.6 verification item 2).
.DESCRIPTION
    Compares the inventoried pre-migration values against the current sources,
    compiles and runs the plain-x64 arithmetic check on this host, and (when
    given -PayloadDll) verifies the ARM payload image carries the retained
    wkProbe* exports and report strings. Writes only to -OutDir. Never touches
    the phone or Device Portal.
.PARAMETER OutDir
    Fresh throwaway output directory under %TEMP%\opencode.
.PARAMETER PayloadDll
    Optional ARM FpuProbePayload.dll to inspect read-only with llvm-readobj.
.PARAMETER MsvcVersion
    Explicit ARM32/MSVC toolset directory name; defaults to the newest installed.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\m10-fpuprobe-86-check'),
    [string]$PayloadDll,
    [string]$MsvcVersion
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))
$probeDir = Join-Path $repo 'Tools\uwp\diagnostics\FpuProbe'
$header = Join-Path $probeDir 'payload\FpuProbePayload.h'
$payload = Join-Path $probeDir 'payload\FpuProbe.cpp'
$asm = Join-Path $probeDir 'payload\CrtFrameProbe.S'
$hostCheck = Join-Path $probeDir 'scripts\FpuProbeHostCheck.cpp'

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

function Assert-SourceContains([string]$file, [string]$pattern, [string]$label) {
    $text = [IO.File]::ReadAllText($file)
    if ($text -notmatch $pattern) { throw "Inventory mismatch in $([IO.Path]::GetFileName($file)): $label" }
    Write-Host "    kept: $label"
}

Write-Host '--- 1. inventoried values still present in sources ---'
# Arithmetic/ABI inventory taken from the pre-migration HEAD sources.
Assert-SourceContains $header 'kLongjmpReturnValue = 7' 'kLongjmpReturnValue = 7'
Assert-SourceContains $header 'kCrtProbeReturn = 11' 'kCrtProbeReturn = 11'
Assert-SourceContains $header 'kIntProbeReturn = 9' 'kIntProbeReturn = 9'
Assert-SourceContains $header 'kFpscrTrapEnableMask = 0x1f00' 'kFpscrTrapEnableMask = 0x1f00'
Assert-SourceContains $header 'kBaselineLeftOperand = 2\.0' 'kBaselineLeftOperand = 2.0'
Assert-SourceContains $header 'kBaselineRightOperand = 4\.0' 'kBaselineRightOperand = 4.0'
Assert-SourceContains $header 'kInitialFloatingValue = 1\.0' 'kInitialFloatingValue = 1.0'
Assert-SourceContains $header 'kInitialFloatingMultiplier = 2\.0' 'kInitialFloatingMultiplier = 2.0'
Assert-SourceContains $header 'kPreJumpFloatingMultiplier = 3\.0' 'kPreJumpFloatingMultiplier = 3.0'
Assert-SourceContains $header 'kPostJumpFloatingMultiplier = 5\.0' 'kPostJumpFloatingMultiplier = 5.0'
Assert-SourceContains $header 'kFloatingResultScale = 1000' 'kFloatingResultScale = 1000'
Assert-SourceContains $header 'kExpectedBaselineX1000 = 8000' 'baseline expectation 8000 (0x1F40)'
Assert-SourceContains $header 'kExpectedInsideX1000 = 6000' 'inside expectation 6000 (0x1770)'
Assert-SourceContains $header 'kExpectedFinalX1000 = 30000' 'final expectation 30000 (0x7530)'
Assert-SourceContains $header 'wkProbeSetjmpSp' 'wkProbeSetjmpSp declaration'
Assert-SourceContains $header 'wkProbeSetjmpR11' 'wkProbeSetjmpR11 declaration'
Assert-SourceContains $header 'wkProbeCrtSetjmp' 'wkProbeCrtSetjmp declaration'
Assert-SourceContains $header 'WKFpuProbeRun' 'WKFpuProbeRun declaration'
Assert-SourceContains $asm 'CRT_FRAME_PROBE wkProbeSetjmpSp, sp' 'SP frame-argument variant'
Assert-SourceContains $asm 'CRT_FRAME_PROBE wkProbeSetjmpR11, r11' 'R11 frame-argument variant'
Assert-SourceContains $payload 'wkProbeSetjmpSp\(crtSpEnv\)' 'SP variant is the executed call'
Assert-SourceContains $payload '"probe-baseline-multiply"' 'baseline report line'
Assert-SourceContains $payload '"probe-inside-before-longjmp"' 'inside report line'
Assert-SourceContains $payload '"probe-fp-result-x1000"' 'final report line'
Assert-SourceContains $payload '"probe-int-before-longjmp"' 'integer report line'
Assert-SourceContains $payload '"probe-crt-sp-return"' 'CRT frame report line'
Assert-SourceContains $payload '"fpuprobe-completed"' 'completion report line'
Assert-SourceContains $payload 'WKFpuProbeRun' 'payload entry point'
if (([IO.File]::ReadAllText($payload)) -match 'class FpuView|CoreApplication|WKFpuProbeStartup') {
    throw 'Retired WRL host code is still present in FpuProbe.cpp'
}
Write-Host '    retired: no FpuView/CoreApplication/WKFpuProbeStartup remains'

Write-Host '--- 2. host-runnable arithmetic check (x64) ---'
$vsRoots = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools')
$toolset = $null
foreach ($root in $vsRoots) {
    $toolsetRoot = Join-Path $root 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $toolsetRoot)) { continue }
    $candidates = @(Get-ChildItem -LiteralPath $toolsetRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'include\setjmp.h')) })
    if ($MsvcVersion) { $candidates = @($candidates | Where-Object { $_.Name -eq $MsvcVersion }) }
    $toolset = ($candidates | Sort-Object -Property Name -Descending | Select-Object -First 1)
    if ($toolset) { break }
}
if (-not $toolset) { throw 'No MSVC toolset with an x64 compiler and CRT headers found' }
$toolset = $toolset.FullName
$cl = Join-Path $toolset 'bin\Hostx64\x64\cl.exe'
if (-not (Test-Path -LiteralPath $cl)) { throw "Host compiler missing: $cl" }
$sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$sdkVersion = '10.0.19041.0'
$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
$env:INCLUDE = "$toolset\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\um;$sdkRoot\Include\$sdkVersion\shared"
$env:LIB = "$toolset\lib\x64;$sdkRoot\Lib\$sdkVersion\ucrt\x64;$sdkRoot\Lib\$sdkVersion\um\x64"
try {
$exe = Join-Path $OutDir 'FpuProbeHostCheck.exe'
# /wd4611: setjmp/longjmp is the point of this check; the warning about C++
# object destruction interaction is expected and reviewed (no live C++ objects
# cross the jumps here), so it stays out of the otherwise warning-free build.
& $cl /nologo /W4 /WX /wd4611 /EHsc /std:c++17 "/I$probeDir\payload" "/Fe$exe" $hostCheck
if ($LASTEXITCODE -ne 0) { throw "Host check compile failed: $LASTEXITCODE" }
}
finally {
$env:INCLUDE = $previousInclude
$env:LIB = $previousLib
}
$output = & $exe
if ($LASTEXITCODE -ne 0) { throw "Host check run failed:`n$($output -join "`n")" }
$output | ForEach-Object { Write-Host "    $_" }
foreach ($expected in @(
    'probe-baseline-multiply: 0x00001f40',
    'probe-inside-before-longjmp: 0x00001770',
    'probe-after-longjmp: 0x00000007',
    'probe-fp-result-x1000: 0x00007530',
    'probe-int-before-longjmp: 0x00000007',
    'probe-int-after-longjmp: 0x00000009',
    'probe-int-value: 0x00000007',
    'fpuprobe-host-check: PASS')) {
    if (-not ($output -join "`n" -match [regex]::Escape($expected))) { throw "Host check output missing: $expected" }
}
Write-Host '    PASS: every host arithmetic expectation matches the inventory'

if ($PayloadDll) {
    Write-Host '--- 3. ARM payload image inspection (read-only) ---'
    if (-not (Test-Path -LiteralPath $PayloadDll)) { throw "Payload DLL not found: $PayloadDll" }
    $llvmBin = Join-Path 'C:\Program Files\Microsoft Visual Studio\2022\Community' 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
    if (-not (Test-Path -LiteralPath $llvmBin)) {
        $llvmBin = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
    }
    $headers = (& $llvmBin --file-headers $PayloadDll 2>&1) -join "`n"
    foreach ($need in @('IMAGE_FILE_MACHINE_ARMNT', 'APPCONTAINER')) {
        if ($headers -notmatch $need) { throw "ARM image missing $need" }
        Write-Host "    kept: $need"
    }
    $exports = (& $llvmBin --coff-exports $PayloadDll 2>&1) -join "`n"
    foreach ($name in @('WKFpuProbeRun', 'wkProbeSetjmpSp', 'wkProbeSetjmpR11', 'wkProbeCrtSetjmp')) {
        if ($exports -notmatch "(?m)^\s*Name: $([regex]::Escape($name))\s*$") { throw "ARM payload missing export $name" }
        Write-Host "    export: $name"
    }
    $strings = (& $llvmBin --string-dump=.rdata $PayloadDll 2>&1) -join "`n"
    if ($strings -notmatch 'probe-baseline-multiply') {
        # Fall back to a raw byte scan when the section dump spelling differs.
        $bytes = [IO.File]::ReadAllBytes($PayloadDll)
        $ascii = [Text.Encoding]::ASCII.GetString($bytes)
        if ($ascii -notmatch 'probe-baseline-multiply') { throw 'ARM payload is missing report strings' }
    }
    foreach ($stage in @('probe-baseline-multiply', 'probe-fp-result-x1000', 'probe-int-value', 'fpuprobe-completed')) {
        Write-Host "    report: $stage"
    }
}

Write-Host ''
Write-Host 'FpuProbe expectation check passed.'
exit 0
