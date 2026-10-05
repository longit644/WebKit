<#
.SYNOPSIS
    Builds the browser-free ARM32 C++/CX XAML probe end to end and checks activation.

.DESCRIPTION
    Reproduces the three steps that make a /ZW AppContainer component activatable on
    this toolchain, in the order they must run:

      1. compile  XamlProbeComponent.cpp with /ZW for ARM32, producing an object
                 that carries a .cormeta section and the CX ABI glue.
      2. metadata run midlrt over XamlProbe.idl to produce the component WinMD and
                 its header. The WinMD declares the runtime class, its default
                 interface and its threading model; it is not optional decoration.
      3. link     the object against the Store flavor of the ARM32 MSVC runtime,
                 exporting DllGetActivationFactory.

    Every flag here was established by direct experiment, because several of them
    are counter-intuitive and their absence fails in misleading ways. In
    particular:

      - /ZW does NOT generate DllGetActivationFactory. Compiling with /ZW alone
        yields an object with no such symbol and no /EXPORT directive, and the
        resulting DLL exports nothing. Activation in this design comes from the
        hand-written WRL IActivationFactory in the component source, and the
        /export: flag is what makes it reachable. Do not "simplify" this by
        expecting the compiler to derive it.
      - The link must use the Store flavor (lib/arm/store), not the desktop one.
        The desktop libvcruntime defines __vcrt_initialize but also drags in
        desktop-only EH and PAL (RtlUnwindEx, __GSHandlerCheck, _fltused) that an
        AppContainer cannot resolve.
      - midlrt needs a host cl.exe on PATH to run the preprocessor, resolves the
        IDL's imports only from Include\<version>\winrt, requires /metadata_dir to
        point at UnionMetadata rather than References, and writes its output
        relative to the current directory. It rejects an absolute /out path.

    The script fails rather than guessing. If a path is missing it reports which
    capability is absent instead of silently producing a partial build.

.PARAMETER MsvcVersion
    Explicit ARM32 MSVC toolset directory name. Defaults to the newest installed.

.PARAMETER SdkVersion
    Explicit Windows SDK version directory. Defaults to the newest that still ships
    the ARM32 import libraries.

.PARAMETER OutDir
    Directory receiving the intermediate and final artifacts.

.EXAMPLE
    .\build-arm32-cx-probe.ps1
    .\build-arm32-cx-probe.ps1 -MsvcVersion 14.41.34120 -Verbose
#>
[CmdletBinding()]
param(
    [string]$MsvcVersion,
    [string]$SdkVersion,

    [string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-probe-arm32')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This file lives in Tools/uwp/build, so the repository root is three levels up.
$script:RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:ComponentDir = Join-Path $script:RepoRoot 'Tools\uwp\CXComponent'
$script:ComponentSource = Join-Path $script:ComponentDir 'XamlProbeComponent.cpp'
$script:ComponentIdl = Join-Path $script:ComponentDir 'XamlProbe.idl'
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$script:VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022'
$script:VsInstance = Join-Path $script:VsRoot 'BuildTools'

# Runtime flavor that an AppContainer component links. The desktop flavor is
# deliberately not a fallback: see the Store-flavor note in the description.
$script:ArmStoreRuntimeDirectory = 'store'

# Libraries an ARM32 C++/CX component needs, in the Store flavor only.
$script:CxRuntimeLibraries = @('vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib', 'vcruntime.lib')

# Entry points the component source defines and the link must export explicitly.
$script:ActivationExport = 'DllGetActivationFactory'
$script:LifetimeExport = 'DllCanUnloadNow'

function Get-LatestDirectory {
    # Newest directory under a parent whose name satisfies a test.
    param(
        [Parameter(Mandatory = $true)][string]$Parent,
        [Parameter(Mandatory = $true)][scriptblock]$Test,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Parent)) {
        throw "$Description not found: $Parent"
    }

    $matches = Get-ChildItem -LiteralPath $Parent -Directory |
        Where-Object $Test |
        Sort-Object -Property Name -Descending
    if (-not $matches) {
        throw "${Description}: no candidate under $Parent satisfies the required layout"
    }
    return $matches[0]
}

function Get-ArmSdk {
    # The header trees are architecture neutral; only the import libraries are
    # per-architecture, so the ARM32 test is Lib\<version>\um\arm and ucrt\arm.
    param([string]$Explicit)

    if ($Explicit) {
        $root = Join-Path $script:WindowsKitsRoot "Lib\$Explicit"
        if (-not (Test-Path -LiteralPath (Join-Path $root 'um\arm'))) {
            throw "Requested SDK $Explicit does not ship ARM32 libraries at $root"
        }
        return [pscustomobject]@{ Version = $Explicit; Root = $root }
    }

    $newest = Get-LatestDirectory -Parent (Join-Path $script:WindowsKitsRoot 'Lib') `
        -Description 'ARM32 Windows SDK' -Test {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'um\arm')) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\arm'))
        }

    return [pscustomobject]@{ Version = $newest.Name; Root = $newest.FullName }
}

function Get-ArmToolset {
    param([string]$Explicit)

    $toolsetRoot = Join-Path $script:VsInstance 'VC\Tools\MSVC'
    $candidates = Get-ChildItem -LiteralPath $toolsetRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm\cl.exe') }
    if (-not $candidates) {
        throw "No MSVC toolset with an ARM32 cl.exe under $toolsetRoot"
    }

    if ($Explicit) {
        $requested = $candidates | Where-Object { $_.Name -eq $Explicit } | Select-Object -First 1
        if (-not $requested) {
            throw "Toolset $Explicit is not installed; available: $(($candidates.Name) -join ', ')"
        }
        return $requested.FullName
    }

    return ($candidates | Sort-Object -Property Name -Descending | Select-Object -First 1).FullName
}

function Get-PlatformWinmdDirectory {
    # Returns the directory holding platform.winmd, which /ZW requires.
    #
    # The metadata lives under a host-architecture path and is present only in
    # newer toolsets. If the selected ARM toolset lacks it, borrow the directory
    # from the newest toolset that has one instead of failing: projection metadata
    # is compiler-version independent, whereas the ARM32 cl.exe is not.
    #
    # The directory, not the file, is returned because that is what /AI takes.
    param([Parameter(Mandatory = $true)][string]$ToolsetRoot)

    $own = Join-Path $ToolsetRoot 'lib\x86\store\references'
    if (Test-Path -LiteralPath (Join-Path $own 'platform.winmd')) {
        return $own
    }

    $siblings = Get-ChildItem -LiteralPath (Split-Path -Parent $ToolsetRoot) -Directory |
        Sort-Object -Property Name -Descending
    foreach ($sibling in $siblings) {
        $candidate = Join-Path $sibling.FullName 'lib\x86\store\references'
        if (Test-Path -LiteralPath (Join-Path $candidate 'platform.winmd')) {
            Write-Host "    platform.winmd borrowed from toolset $($sibling.Name)"
            return $candidate
        }
    }
    return $null
}

function ConvertTo-QuotedArgument {
    # PowerShell does not add quotes when it builds a native command line, so a
    # path containing spaces arrives at the tool split into several arguments.
    # Every path here lives under "Program Files (x86)", so quoting is mandatory.
    #
    # For a compiler-style option the quotes belong around the path only, as in
    # /AI"C:\path\to\platform.winmd". Wrapping the whole argument instead would
    # hide the option itself from the tool's parser.
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
    # Runs a native tool, echoes its command line, and converts a non-zero exit
    # code into a thrown error naming the step.
    param(
        [Parameter(Mandatory = $true)][string]$Step,
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][hashtable]$Environment
    )

    Write-Host "  $Step"
    Write-Host "    $Exe $($Arguments -join ' ')"

    $previous = @{}
    foreach ($name in $Environment.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $Environment[$name], 'Process')
    }

    $invocation = @($Arguments | ForEach-Object { ConvertTo-QuotedArgument -Argument $_ })

    # These SDK tools write their banner to stderr, and PowerShell turns a native
    # stderr write into a terminating NativeCommandError when ErrorActionPreference
    # is Stop. Preference is relaxed for the duration so the tool's own exit code
    # decides success, rather than a harmless banner aborting the build.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'

    try {
        Push-Location $WorkingDirectory
        try {
            $output = & $Exe @invocation 2>&1
            $exitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $ErrorActionPreference = $previousPreference
        foreach ($name in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }

    # midlrt narrates every IDL it parses, which is useful on failure and pure
    # noise on success, so it is summarized rather than echoed line by line.
    foreach ($line in $output) {
        if ($Step -like 'midlrt*' -and "$line" -notmatch 'error|warning') {
            continue
        }
        Write-Host "      $line"
    }

    if ($exitCode -ne 0) {
        throw "$Step failed with exit code $exitCode"
    }
}

if (-not (Test-Path -LiteralPath $script:ComponentSource)) {
    throw "Component source not found: $script:ComponentSource"
}
if (-not (Test-Path -LiteralPath $script:ComponentIdl)) {
    throw "Component IDL not found: $script:ComponentIdl"
}

$sdk = Get-ArmSdk -Explicit $SdkVersion
$toolset = Get-ArmToolset -Explicit $MsvcVersion
$platformWinmdDirectory = Get-PlatformWinmdDirectory -ToolsetRoot $toolset
if (-not $platformWinmdDirectory) {
    throw "platform.winmd not found in any installed toolset; /ZW cannot resolve its projection types"
}

$storeLibDir = Join-Path $toolset "lib\arm\$script:ArmStoreRuntimeDirectory"
foreach ($library in $script:CxRuntimeLibraries) {
    $path = Join-Path $storeLibDir $library
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Store flavor runtime library missing: $path"
    }
}

$armCl = Join-Path $toolset 'bin\Hostx64\arm\cl.exe'

# midlrt needs a host cl.exe on PATH purely to run the preprocessor. The ARM
# component toolset does not necessarily install a host one, so any installed
# toolset's host directory will do; fall back to the instance's directory layout
# only when none has one, and let the tool-missing check below report it.
$hostClDir = (Get-ChildItem -LiteralPath (Split-Path -Parent $toolset) -Directory |
    Sort-Object -Property Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x86' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'cl.exe') } |
    Select-Object -First 1)

$lldLink = Join-Path $script:VsInstance 'VC\Tools\Llvm\x64\bin\lld-link.exe'
$midlrt = Join-Path $script:WindowsKitsRoot "bin\$($sdk.Version)\x86\midlrt.exe"

if (-not $hostClDir) {
    throw "No host cl.exe found under any installed toolset; midlrt cannot run its preprocessor"
}

foreach ($required in @($armCl, $lldLink, $midlrt)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required tool not found: $required"
    }
}

$sdkInclude = Join-Path $script:WindowsKitsRoot "Include\$($sdk.Version)"

# Union metadata, not the References tree: References holds split per-contract
# winmds with no union Windows.winmd, and both cl and midlrt need the union.
$unionMetadata = Join-Path $script:WindowsKitsRoot "UnionMetadata\$($sdk.Version)"
if (-not (Test-Path -LiteralPath (Join-Path $unionMetadata 'Windows.winmd'))) {
    throw "SDK union metadata missing at $unionMetadata; the split References tree cannot satisfy cl or midlrt"
}

$oneCoreUap = Join-Path $sdk.Root 'um\arm\OneCoreUAP.lib'
if (-not (Test-Path -LiteralPath $oneCoreUap)) {
    throw "OneCoreUAP.lib missing: $oneCoreUap"
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$objDir = Join-Path $OutDir 'obj'
$metadataDir = Join-Path $OutDir 'metadata'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
New-Item -ItemType Directory -Path $metadataDir -Force | Out-Null

$objectFile = Join-Path $objDir 'XamlProbeComponent.obj'
$dllPath = Join-Path $OutDir 'XamlProbeComponent.dll'

Write-Host 'ARM32 C++/CX XAML probe'
Write-Host "  msvc toolset     : $toolset"
Write-Host "  sdk version      : $($sdk.Version)"
Write-Host "  platform.winmd   : $(Join-Path $platformWinmdDirectory 'platform.winmd')"
Write-Host "  output           : $dllPath"
Write-Host ''

# Step 1: compile. /ZW carries the CX language extensions and emits the metadata
# section; /AI points at platform.winmd and the SDK union metadata, without which
# the compiler has no projection types at all.
Write-Host '1/3 compile'
Invoke-Checked -Step 'cl /ZW (ARM32)' -Exe $armCl -WorkingDirectory $script:RepoRoot -Environment @{
    INCLUDE = @(
        (Join-Path $toolset 'include'),
        (Join-Path $sdkInclude 'ucrt'),
        (Join-Path $sdkInclude 'um'),
        (Join-Path $sdkInclude 'shared'),
        (Join-Path $sdkInclude 'winrt')
    ) -join ';'
} -Arguments @(
    '/nologo', '/c', '/ZW', '/EHsc', '/MD',
    # /AI names the directory holding the assembly, not the assembly file. Passing
    # platform.winmd itself yields "C1107: could not find assembly 'platform.winmd'",
    # which reads as if the reference is missing when it is present.
    ('/AI' + $platformWinmdDirectory),
    ('/AI' + $unionMetadata),
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00',
    ('/I' + $script:ComponentDir),
    ('/Fo' + $objectFile),
    $script:ComponentSource
)

if (-not (Test-Path -LiteralPath $objectFile)) {
    throw "cl reported success but produced no object at $objectFile"
}

# Step 2: WinMD. midlrt resolves imports only from the winrt include folder and
# needs a host cl.exe on PATH for its preprocessor. It writes beside the current
# directory, so it runs in the metadata directory and no /out is passed.
Write-Host '2/3 generate WinMD'
Invoke-Checked -Step 'midlrt /winrt' -Exe $midlrt -WorkingDirectory $metadataDir -Environment @{
    INCLUDE = @(
        (Join-Path $sdkInclude 'winrt'),
        (Join-Path $sdkInclude 'shared'),
        (Join-Path $sdkInclude 'um')
    ) -join ';'
    PATH = "$hostClDir;$env:PATH"
} -Arguments @(
    '/winrt', '/nomidl', '/ns_prefix', '/server', 'none', '/client', 'none',
    ('/metadata_dir' + $unionMetadata),
    $script:ComponentIdl
)

$winmd = Join-Path $metadataDir 'XamlProbe.winmd'
$generatedHeader = Join-Path $metadataDir 'XamlProbe.h'
foreach ($expected in @($winmd, $generatedHeader)) {
    if (-not (Test-Path -LiteralPath $expected)) {
        throw "midlrt reported success but produced no $expected"
    }
}
Write-Host "    winmd: $((Get-Item -LiteralPath $winmd).Length) bytes"
Write-Host "    header: $generatedHeader"

# Step 3: link. The Store flavor supplies __vcrt_initialize and the vector
# destructor EH helper; the exports are explicit because /ZW derives neither
# activation nor the module-lifetime query.
Write-Host '3/3 link'
Invoke-Checked -Step 'lld-link activation/lifetime exports' -Exe $lldLink -WorkingDirectory $OutDir -Environment @{} `
    -Arguments (@(
    '/nologo', '/dll', '/machine:arm', '/appcontainer', '/force:multiple',
    ('/out:' + $dllPath),
    ('/libpath:' + $storeLibDir),
    ('/libpath:' + (Join-Path $sdk.Root 'um\arm')),
    ('/libpath:' + (Join-Path $sdk.Root 'ucrt\arm')),
    $objectFile
    ) + $script:CxRuntimeLibraries + @($oneCoreUap,
        ('/export:' + $script:ActivationExport), ('/export:' + $script:LifetimeExport)))

if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "lld-link reported success but produced no DLL at $dllPath"
}

# Verify the produced binary rather than trusting the exit code: a component that
# does not export either entry point links cleanly but cannot activate or report
# its unload contract, which is exactly the failure this script exists to prevent.
$readobj = Join-Path $script:VsInstance 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'
$dumpbin = Join-Path $toolset 'bin\Hostx64\arm\dumpbin.exe'

# llvm-readobj writes its report to stdout but PowerShell surfaces some of it as
# error records, so the text is collected rather than filtered in place.
$headerText = (& $readobj --file-headers $dllPath 2>&1 | Out-String)
if ($headerText -notmatch 'IMAGE_FILE_MACHINE_ARMNT') {
    throw "DLL is not ARMNT; refusing to report a Windows on ARM component as built"
}
if ($headerText -notmatch 'APPCONTAINER') {
    throw "DLL lacks IMAGE_DLL_CHARACTERISTICS_APPCONTAINER; it would not be AppContainer-capable"
}
if ($headerText -notmatch 'COFF-ARM') {
    throw "DLL is not COFF-ARM; the ARM32 target was not honoured"
}
Write-Host ''
Write-Host 'Verified:'
Write-Host '  format           : COFF-ARM'
Write-Host '  machine          : IMAGE_FILE_MACHINE_ARMNT (ARM32, thumb)'
Write-Host '  appcontainer     : yes'

$exports = & $dumpbin /exports $dllPath 2>&1 | Out-String
if ($exports -notmatch [regex]::Escape($script:ActivationExport)) {
    throw "DLL does not export $script:ActivationExport; the probe would link but never activate"
}
if ($exports -notmatch [regex]::Escape($script:LifetimeExport)) {
    throw "DLL does not export $script:LifetimeExport; the host cannot honor the module lifetime contract"
}
Write-Host "  exports          : $script:ActivationExport, $script:LifetimeExport"

Write-Host ''
Write-Host "Component DLL built: $dllPath ($((Get-Item -LiteralPath $dllPath).Length) bytes)"
Write-Host "Component WinMD:    $winmd"
exit 0
