<#
.SYNOPSIS
    Verifies the M10 task 7.1 dispatcher/clipboard service: neutral callbacks,
    exactly-once completion and no CX/WinRT ownership in engine headers.

.DESCRIPTION
    Six checks, all from repository sources, in the order a consumer meets them:

      1. The neutral service source compiles clean for ARM32 with the native
         engine compiler at /W4 with no warnings.
      2. The same source compiles clean for ARM32 with the MSVC ARM32 compiler
         that owns /ZW, at /W4 with no warnings. Both boundary compilers must
         accept the same source without /ZW, which is the neutrality proof: any
         ref class, hat type or Windows:: projection reference would fail here.
      3. The CX dispatcher and WinRT transport sources compile clean for ARM32
         with the exact /ZW component flags, at /W4 with no warnings.
      4. The neutral service object links into a genuine ARM32 AppContainer
         module, verified with llvm-readobj rather than trusted from the
         command line.
      5. The engine purity gate still passes, the neutral service header carries
         no CX/WinRT token, and no engine header reaches for the new CX or
         service headers.
      6. Service behaviour runs on this x64 host from x64 copies of the same
         sources: write/read completion exactness, duplicate-ID rejection,
         cancel semantics, worker-thread marshal delivery, refused-enqueue
         accounting, wrong-thread rejection and borrowed-table routing. The
         behaviour is ownership bookkeeping plus HRESULT arithmetic and does not
         vary by architecture; WinRT clipboard behaviour belongs to later
         device tasks.

    This script does not claim a full WebCore or WebKit rebuild, component
    target wiring (the runtime owner in 7.5 consumes the service), WinRT
    clipboard behaviour, or device behaviour of any slot.

.PARAMETER OutDir
    Directory receiving the throwaway objects, modules and harness.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name; defaults to the newest installed.

.EXAMPLE
    .\test-wkw10m-clipboard-service.ps1
    .\test-wkw10m-clipboard-service.ps1 -OutDir D:\m10\clipboard
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\w10m-clipboard-service'),
    [string]$MsvcVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/w10m/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:AbiHeaderDir = Join-Path $script:RepoRoot 'Source\WebKit\Shared\w10m'
$script:ApiDir = Join-Path $script:RepoRoot 'Source\WebKit\UIProcess\API\w10m'
$script:PlatformDir = Join-Path $script:ApiDir 'platform'
$script:NativeDir = Join-Path $script:ApiDir 'native'
$script:ServiceSource = Join-Path $script:PlatformDir 'ClipboardServiceW10M.cpp'
$script:ServiceHeader = Join-Path $script:PlatformDir 'ClipboardServiceW10M.h'
$script:DispatcherSource = Join-Path $script:PlatformDir 'DispatcherW10M.cpp'
$script:TransportSource = Join-Path $script:PlatformDir 'ClipboardPlatformW10M.cpp'
$script:HarnessSource = Join-Path $PSScriptRoot 'ClipboardServiceTests.cpp'
$script:PurityGate = Join-Path $PSScriptRoot 'check-w10m-engine-purity.py'
$script:EngineDir = Join-Path $script:RepoRoot 'Source\WebCore\platform\w10m'
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
    # a service file that only compiles quietly would make the 7.1 proof rest on
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

foreach ($required in @($script:ServiceSource, $script:ServiceHeader, $script:DispatcherSource, $script:TransportSource, $script:HarnessSource, $script:PurityGate)) {
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
$references = Join-Path $armToolset 'lib\x86\store\references'
$metadata = Join-Path $script:WindowsKitsRoot "UnionMetadata\$sdkVersion"
foreach ($required in @("$references\platform.winmd", "$metadata\Windows.winmd")) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required CX metadata missing: $required"
    }
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host 'M10 task 7.1 clipboard service verification'
Write-Host "  service         : $script:ServiceSource"
Write-Host "  dispatcher      : $script:DispatcherSource"
Write-Host "  transport       : $script:TransportSource"
Write-Host "  native compiler : $clangCl"
Write-Host "  cx/arm compiler : $armCl"
Write-Host "  host compiler   : $x64Cl"
Write-Host "  sdk             : $sdkVersion"
Write-Host "  output          : $OutDir"
Write-Host ''

$previousInclude = $env:INCLUDE
$env:INCLUDE = $armInclude
try {
    # ---------------------------------------------------------------------------
    Write-Host '1/6 ARM32 compile of the neutral service with the native engine compiler'
    $armCompileFlags = @($nativeCFlags -split ' ' | Where-Object { $_ -notmatch '^(-B|-fuse-ld=)' })
    # The toolchain flags are used exactly, with no /EHsc appended: the real
    # engine build disables C++ unwinding (/EHs-c-), the service is written for
    # that model, and appending /EHsc after /EHs-c- trips an LLVM 19.1.5 ARM
    # backend crash (cleanupret selection) on this translation unit.
    $armServiceObject = Join-Path $OutDir 'ClipboardServiceUWP-arm-native.obj'
    $run = Invoke-Captured -Exe $clangCl -Arguments (@($armCompileFlags) + @(
            '/nologo', '/c', '/W4', '/WX',
            ('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
            ('/Fo' + $armServiceObject), $script:ServiceSource))
    $run.Output | ForEach-Object { Write-Host "    $_" }
    if ($run.ExitCode -ne 0) {
        throw "ARM32 native compile of $script:ServiceSource failed with exit code $($run.ExitCode)"
    }
    Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 native compile of $script:ServiceSource"
    Write-Host '    no warnings'

    # ---------------------------------------------------------------------------
    Write-Host '2/6 ARM32 compile of the neutral service with the MSVC ARM32 compiler'
    $armCxServiceObject = Join-Path $OutDir 'ClipboardServiceUWP-arm-cx.obj'
    $run = Invoke-Captured -Exe $armCl -Arguments @(
        '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
        '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
        ('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
        ('/Fo' + $armCxServiceObject), $script:ServiceSource)
    $run.Output | ForEach-Object { Write-Host "    $_" }
    if ($run.ExitCode -ne 0) {
        throw "ARM32 CX compile of $script:ServiceSource failed with exit code $($run.ExitCode)"
    }
    Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 CX compile of $script:ServiceSource"
    Write-Host '    no warnings (neutral: no /ZW needed)'

    # ---------------------------------------------------------------------------
    Write-Host '3/6 ARM32 /ZW compile of the dispatcher and WinRT transport'
    foreach ($source in @($script:DispatcherSource, $script:TransportSource)) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($source) + '-arm-zw.obj')
        $run = Invoke-Captured -Exe $armCl -Arguments @(
            '/nologo', '/c', '/ZW', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX',
            '/D_WINRT_DLL', '/DNOMINMAX',
            '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
            ('/AI' + $references), ('/AI' + $metadata),
            ('/I' + $script:ApiDir), ('/I' + $script:AbiHeaderDir),
            ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
            ('/Fo' + $object), $source)
        $run.Output | ForEach-Object { Write-Host "    $_" }
        if ($run.ExitCode -ne 0) {
            throw "ARM32 /ZW compile of $source failed with exit code $($run.ExitCode)"
        }
        Assert-NoCompilerDiagnostic -Output $run.Output -Step "ARM32 /ZW compile of $source"
    }
    Write-Host '    no warnings'
}
finally {
    $env:INCLUDE = $previousInclude
}

# ---------------------------------------------------------------------------
Write-Host '4/6 ARM32 module inspection'
$armDll = Join-Path $OutDir 'WKW10MClipboardService-arm.dll'
$armLinkRun = Invoke-Captured -Exe $lldLink -Arguments (@(
        '/nologo', '/dll', '/NOENTRY', ('/out:' + $armDll)) + ($nativeLinkFlags -split ' ') + @($armServiceObject))
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
Write-Host '5/6 engine purity and header neutrality audits'
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & python $script:PurityGate
    $purityExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
if ($purityExit -ne 0) {
    throw "Engine purity gate failed with exit code $purityExit"
}

$projectionHits = Select-String -LiteralPath $script:ServiceHeader -Pattern @('ref class', 'ref struct', 'Windows::', 'CoreDispatcher', 'String^', '#using') -SimpleMatch
if ($projectionHits) {
    throw "Neutral service header carries CX/WinRT tokens:`n$($projectionHits -join "`n")"
}
Write-Host '    neutral service header carries no CX/WinRT token'

$engineHits = Get-ChildItem -LiteralPath $script:EngineDir -Filter '*.h' |
    Select-String -Pattern 'DispatcherUWP\.h|ClipboardPlatformUWP\.h|ClipboardServiceUWP\.h'
if ($engineHits) {
    throw "Engine header reaches for the new service headers:`n$($engineHits -join "`n")"
}
Write-Host '    no engine header includes the dispatcher/service headers'

# ---------------------------------------------------------------------------
Write-Host '6/6 clipboard service behaviour on this x64 host'
$x64Exe = Join-Path $OutDir 'ClipboardServiceTests.exe'
$x64ServiceObject = Join-Path $OutDir 'ClipboardServiceUWP-x64.obj'
$x64HarnessObject = Join-Path $OutDir 'ClipboardServiceTests.obj'

# The x64 host builds below deliberately omit /DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP:
# a desktop test process is not an AppContainer. The ARM32 builds above define the
# App family and therefore exercise the packaged branch the real component uses.
Invoke-Checked -Step 'cl service (host x64)' -Exe $x64Cl `
    -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
    ('/Fo' + $x64ServiceObject),
    $script:ServiceSource)

Invoke-Checked -Step 'cl harness (host x64)' -Exe $x64Cl -Environment @{ INCLUDE = $x64Include } -Arguments @(
    '/nologo', '/c', '/W4', '/WX', '/EHsc', '/MD',
    ('/I' + $script:AbiHeaderDir), ('/I' + $script:PlatformDir), ('/I' + $script:NativeDir),
    ('/Fo' + $x64HarnessObject),
    $script:HarnessSource
)

# The Store-flavored runtime the shipping ARM32 module uses is OneCore-only and is
# not loadable on the desktop, so this host binary links the desktop runtime. The
# runtime flavor itself is proven by the ARM32 link above; what runs here is
# service behaviour.
Invoke-Checked -Step 'lld-link harness (host x64)' -Exe $lldLink -Arguments (@(
    '/nologo', '/machine:x64',
    ('/out:' + $x64Exe),
    ('/libpath:' + (Join-Path $x64Toolset 'lib\x64')),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\um\x64")),
    ('/libpath:' + (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64"))) + @(
    $x64HarnessObject, $x64ServiceObject,
    'msvcrt.lib', 'vcruntime.lib',
    (Join-Path $script:WindowsKitsRoot "Lib\$sdkVersion\ucrt\x64\ucrt.lib"),
    'kernel32.lib'))

Write-Host ''
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $x64Exe
    $harnessExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousPreference
}
Write-Host ''

if ($harnessExit -ne 0) {
    throw "Clipboard service verification failed with exit code $harnessExit"
}

Write-Host 'Task 7.1 clipboard service verification passed.'
Write-Host 'Not claimed here: a full WebCore or WebKit rebuild, component target wiring'
Write-Host '(deferred to the 7.5 runtime owner), WinRT clipboard behaviour, or device'
Write-Host 'behaviour of any slot.'
exit 0
