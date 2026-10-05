<#
.SYNOPSIS
    Runs the M10 gate 2.4 activation and lifetime verification for the XAML probe.

.DESCRIPTION
    Task 2.4 requires proving that activation returns a valid HRESULT, that a
    failure during activation is reported rather than fatal, and that reference
    counting and destruction are correct across the success path and every failure
    path.

    The shipping component is ARM32 and an ARM32 image cannot execute on this x64
    development host, so this script builds an x64 copy of the same
    XamlProbeComponent.cpp, builds the harness against it, and runs the harness.
    The behaviour under test is language-level activation and reference counting,
    which does not vary by architecture. This script therefore does not claim to
    prove device behaviour: the ARM32 build is carried on the phone in task 2.6.

    The component is built from the repository source rather than a checked-in
    binary, so the harness always tests what is currently in the tree.

    The harness resolves DllGetActivationFactory with GetProcAddress instead of
    linking against the component, so a component that stops exporting the entry
    point fails the gate rather than being linked in implicitly.

.PARAMETER OutDir
    Directory receiving the component, the harness executable and the generated
    metadata.

.EXAMPLE
    .\test-arm32-cx-probe-lifetime.ps1
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-probe-lifetime')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/uwp/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:ComponentDir = Join-Path $script:RepoRoot 'Tools\uwp\CXComponent'
$script:ComponentSource = Join-Path $script:ComponentDir 'XamlProbeComponent.cpp'
$script:ComponentIdl = Join-Path $script:ComponentDir 'XamlProbe.idl'
$script:HarnessSource = Join-Path $PSScriptRoot 'XamlProbeLifetimeTests.cpp'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'

function Get-LatestToolset {
    param([Parameter(Mandatory = $true)][string]$Parent)

    $matches = Get-ChildItem -LiteralPath $Parent -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') } |
        Sort-Object -Property Name -Descending
    if (-not $matches) {
        throw "No MSVC toolset with a host x64 cl.exe under $Parent"
    }
    return $matches[0].FullName
}

function ConvertTo-QuotedArgument {
    # PowerShell does not quote arguments it passes to a native command, so paths
    # under "Program Files (x86)" would otherwise arrive split at the space.
    # For a compiler-style option the quotes belong around the path only.
    param([Parameter(Mandatory = $true)][string]$Argument)

    if ($Argument -notmatch '[\s()]') {
        return $Argument
    }
    if ($Argument -match '^(/[A-Za-z]+)(.+)$') {
        return $Matches[1] + '"' + $Matches[2] + '"'
    }
    if ($Argument.StartsWith('"')) {
        return $Argument
    }
    return '"' + $Argument + '"'
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Step,
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][hashtable]$Environment
    )

    Write-Host "  $Step"
    $invocation = @($Arguments | ForEach-Object { ConvertTo-QuotedArgument -Argument $_ })

    # The toolchain directories are passed through the environment rather than as
    # /I arguments so that INCLUDE matches what MSBuild would set for a /ZW
    # compilation, including the CX header vccorlib.h that only the toolset ships.
    $previous = @{}
    foreach ($name in $Environment.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $Environment[$name], 'Process')
    }

    # The SDK tools write banners to stderr, which becomes a terminating
    # NativeCommandError under fail-fast scripting. The tool's own exit code
    # decides success instead.
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

foreach ($required in @($script:ComponentSource, $script:ComponentIdl, $script:HarnessSource)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required source not found: $required"
    }
}

$sdkLibRoot = Join-Path $script:WindowsKitsRoot 'Lib'
$sdk = Get-ChildItem -LiteralPath $sdkLibRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\x64') } |
    Sort-Object -Property Name -Descending |
    Select-Object -First 1
if (-not $sdk) {
    throw "No Windows SDK with host x64 libraries under $sdkLibRoot"
}

$toolset = Get-LatestToolset -Parent (Join-Path $script:VsRoot 'VC\Tools\MSVC')
$cl = Join-Path $toolset 'bin\Hostx64\x64\cl.exe'
$link = Join-Path $script:VsRoot 'VC\Tools\Llvm\x64\bin\lld-link.exe'
$midlrt = Join-Path $script:WindowsKitsRoot "bin\$($sdk.Name)\x86\midlrt.exe"

$platformReferences = Join-Path $toolset 'lib\x86\store\references'
if (-not (Test-Path -LiteralPath (Join-Path $platformReferences 'platform.winmd'))) {
    throw "platform.winmd missing at $platformReferences"
}

$sdkInclude = Join-Path $script:WindowsKitsRoot "Include\$($sdk.Name)"
$unionMetadata = Join-Path $script:WindowsKitsRoot "UnionMetadata\$($sdk.Name)"
if (-not (Test-Path -LiteralPath (Join-Path $unionMetadata 'Windows.winmd'))) {
    throw "SDK union metadata missing at $unionMetadata"
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$metadataDir = Join-Path $OutDir 'metadata'
New-Item -ItemType Directory -Path $metadataDir -Force | Out-Null

$componentDll = Join-Path $OutDir 'XamlProbeComponent.dll'
$harnessExe = Join-Path $OutDir 'XamlProbeLifetimeTests.exe'
$componentObj = Join-Path $OutDir 'XamlProbeComponent.obj'
$harnessObj = Join-Path $OutDir 'XamlProbeLifetimeTests.obj'

Write-Host 'M10 gate 2.4 activation and lifetime verification'
Write-Host "  toolset       : $toolset"
Write-Host "  sdk           : $($sdk.Name)"
Write-Host "  output        : $OutDir"
Write-Host "  architecture  : x64 host (the ARM32 image is verified on device in task 2.6)"
Write-Host ''

$includeList = @(
    (Join-Path $toolset 'include'),
    (Join-Path $sdkInclude 'ucrt'),
    (Join-Path $sdkInclude 'um'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'winrt')
) -join ';'

Write-Host '1/4 compile the component'
Invoke-Checked -Step 'cl /ZW (host x64)' -Exe $cl -Environment @{ INCLUDE = $includeList } -Arguments @(
    '/nologo', '/c', '/ZW', '/EHsc', '/MD',
    ('/AI' + $platformReferences),
    ('/AI' + $unionMetadata),
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:ComponentDir),
    ('/Fo' + $componentObj),
    $script:ComponentSource
)

Write-Host '2/4 generate the component metadata'
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'

# midlrt needs a host cl.exe on PATH purely to run the preprocessor, and resolves
# the IDL's imports only from the winrt include folder.
$previousPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
$previousInclude = [Environment]::GetEnvironmentVariable('INCLUDE', 'Process')
[Environment]::SetEnvironmentVariable('PATH',
    (Join-Path $toolset 'bin\Hostx64\x64') + ';' + $previousPath, 'Process')
[Environment]::SetEnvironmentVariable('INCLUDE', @(
    (Join-Path $toolset 'include'),
    (Join-Path $sdkInclude 'winrt'),
    (Join-Path $sdkInclude 'shared'),
    (Join-Path $sdkInclude 'um')
) -join ';', 'Process')

try {
    Push-Location $metadataDir
    try {
        $midlOutput = & $midlrt /winrt /nomidl /ns_prefix /server none /client none `
            ('/metadata_dir' + $unionMetadata) $script:ComponentIdl 2>&1
        $midlExit = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    $ErrorActionPreference = $previousPreference
    [Environment]::SetEnvironmentVariable('PATH', $previousPath, 'Process')
    [Environment]::SetEnvironmentVariable('INCLUDE', $previousInclude, 'Process')
}
foreach ($line in $midlOutput) {
    if ("$line" -match 'error|warning') {
        Write-Host "    $line"
    }
}
if ($midlExit -ne 0) {
    throw "midlrt failed with exit code $midlExit"
}
$generatedHeader = Join-Path $metadataDir 'XamlProbe.h'
if (-not (Test-Path -LiteralPath $generatedHeader)) {
    throw "midlrt produced no $generatedHeader"
}

Write-Host '3/4 link the component and harness'
# Both images are built for the desktop host, so both link the desktop MSVC
# runtime. The Store flavor the shipping component uses is OneCore-only and is not
# loadable on the desktop, which fails at LoadLibrary with ERROR_MOD_NOT_FOUND
# (0x8007007E) rather than at link time. The runtime flavor itself is exercised by
# build-arm32-cx-probe.ps1 and on the device in task 2.6; what is under test here
# is activation and reference counting.
#
# /export:DllGetActivationFactory and /export:DllCanUnloadNow are required:
# /ZW does not add either entry point.
Invoke-Checked -Step 'lld-link component' -Exe $link -Environment @{} -Arguments @(
    '/nologo', '/dll', '/machine:x64',
    ('/out:' + $componentDll),
    ('/libpath:' + (Join-Path $toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $sdk.FullName 'um\x64')),
    ('/libpath:' + (Join-Path $sdk.FullName 'ucrt\x64')),
    $componentObj,
    'vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib', 'vcruntime.lib',
    'runtimeobject.lib', 'ole32.lib', 'kernel32.lib',
    (Join-Path $sdk.FullName 'ucrt\x64\ucrt.lib'),
    '/export:DllGetActivationFactory', '/export:DllCanUnloadNow'
)

# The harness links only against the OS, never against the component, so it can
# only reach activation through the exported entry point.
Invoke-Checked -Step 'cl harness' -Exe $cl -Environment @{ INCLUDE = $includeList } -Arguments @(
    '/nologo', '/c', '/EHsc', '/MD',
    ('/I' + $metadataDir),
    ('/Fo' + $harnessObj),
    $script:HarnessSource
)

Invoke-Checked -Step 'lld-link harness' -Exe $link -Environment @{} -Arguments @(
    '/nologo', '/machine:x64',
    ('/out:' + $harnessExe),
    # The harness runs on the desktop host, so it links the desktop MSVC runtime
    # and the SDK UCRT. The Store flavor used by the component is OneCore-only and
    # is not loadable here, which fails at process start with STATUS_DLL_NOT_FOUND
    # rather than at link time.
    ('/libpath:' + (Join-Path $toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $sdk.FullName 'um\x64')),
    ('/libpath:' + (Join-Path $sdk.FullName 'ucrt\x64')),
    $harnessObj,
    'runtimeobject.lib', 'ole32.lib', 'kernel32.lib',
    'msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib',
    (Join-Path $sdk.FullName 'ucrt\x64\ucrt.lib')
)

Write-Host '4/4 run the verification harness'
Write-Host ''
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $harnessExe $componentDll
    $harnessExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
Write-Host ''

if ($harnessExit -ne 0) {
    throw "Lifetime verification failed with exit code $harnessExit"
}

Write-Host 'Task 2.4 host-side verification passed.'
Write-Host 'Remaining for 2.4: the same checks against the ARM32 build on the phone,'
Write-Host 'which requires the task 2.6 package and deployment.'
exit 0
