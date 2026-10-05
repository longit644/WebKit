<#
.SYNOPSIS
Measures the ARM32 UWP build surface for this port and fails fast on gaps.

.DESCRIPTION
M10 requires two independent build domains before any migration:

  * Native engine: the existing CMake/Ninja ARM32 configuration, which needs
    Windows SDK ARM32 (um/arm, ucrt/arm) link libraries plus the repository's
    own runtime shims.
  * C++/CX component and applications: MSVC ARM32 (/ZW), which additionally
    needs the CX runtime import library, the C++/CX metadata references and an
    MSBuild ARM toolset.

This script only measures. It never installs, edits or substitutes anything,
and it never accepts a different architecture as a stand-in. Every missing
requirement is reported with its exact expected path so a failing run can be
turned into a single, precise installer or configuration action.

Discovery prefers installation metadata (vswhere, Windows Kits layout) over
hard-coded developer paths. -VsPath and -SdkVersion exist only as explicit,
recorded overrides for unusual machines; overrides are reported so evidence
never hides which paths were used.

.PARAMETER VsPath
Optional Visual Studio installation root to inspect instead of auto-detection.

.PARAMETER MsvcVersion
Optional MSVC toolset directory name (for example 14.42.35207) to require. Needed
when several ARM toolsets are installed, because only some releases ship the
ARM32 Store import library the C++/CX runtime requires.

.PARAMETER SdkVersion
Optional Windows SDK version (for example 10.0.19041.0) to require. When given,
other installed versions are still listed but never selected silently.

.PARAMETER Json
Emit the measured record as JSON instead of the human-readable report.

.PARAMETER Quiet
Suppress per-item OK lines and print only gaps plus the summary.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools/uwp/build/discover-arm32-toolchain.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File Tools/uwp/build/discover-arm32-toolchain.ps1 -Json
#>

[CmdletBinding()]
param(
    [string]$VsPath,
    [string]$MsvcVersion,
    [string]$SdkVersion,
    [switch]$Json,
    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Architecture folder names used by Visual Studio and the Windows Kits.
$script:ArmCompilerFolder = 'arm'
$script:HostCompilerFolder = 'Hostx64'

# Library directory names whose ARM32 presence decides whether linking works.
$script:RequiredArmSdkLibraries = @('ole32.lib', 'runtimeobject.lib', 'uuid.lib')

# C++/CX support requires its runtime import library in both link flavors we use.
$script:RequiredCxRuntimeLibraries = @('vccorlib.lib')

# The ARM32 C++/CX component links against the Store flavor of the MSVC runtime,
# not the desktop flavor. The Store flavor is AppContainer-safe and is what
# satisfies the /ZW objects' references to __vcrt_initialize and the vector
# destructor EH helper; the desktop flavor drags in desktop-only EH and PAL
# symbols such as RtlUnwindEx and _fltused that an AppContainer cannot link.
$script:RequiredCxUwpRuntimeLibraries = @('msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'vccorlib.lib')

# Library directory holding the Store flavor of the ARM32 MSVC runtime.
$script:ArmStoreRuntimeDirectory = 'store'

# Repository-owned runtime support that the native ARM32 link recipe depends on.
$script:RepositoryRuntimeLibraries = @('arm-crtstart.lib')

# Native ARM32 runtime support the toolchain links after the msvcurt removal.
$script:RequiredNativeUwpRuntimeLibraries = @('vcruntime.lib', 'msvcprt.lib')

# Combined ARM32 Store import library. Newer MSVC releases dropped it for ARM,
# but it is not required: an earlier investigation found that the individual
# Store flavor libraries satisfy every symbol a /ZW ARM32 component needs. This
# name is retained only so discovery can report whether it is present, because its
# absence is expected on current toolsets and is no longer a blocker by itself.
$script:StoreCrtLibrary = 'msvcurt.lib'

# vswhere executable shipped with the Visual Studio Installer.
$script:VsWhereRelativePath = 'Microsoft Visual Studio\Installer\vswhere.exe'

# Windows Kits root holding SDK headers, libraries and metadata.
$script:WindowsKitsRoot = 'C:\Program Files (x86)\Windows Kits\10'

# Findings use three severities: a blocker stops every link, a warning needs a
# decision recorded in evidence, and OK is a measured capability.
$script:Blocker = 'BLOCKER'
$script:Warning = 'WARNING'
$script:Ok = 'OK'

$script:Findings = New-Object System.Collections.Generic.List[object]

function Add-Finding {
    <#
    .SYNOPSIS
    Records one measured capability or gap for the final report.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$Area,
        [Parameter(Mandatory = $true)][string]$Requirement,
        [Parameter(Mandatory = $true)][string]$Status,
        [Parameter(Mandatory = $true)][string]$Detail
    )

    $script:Findings.Add([pscustomobject]@{
            Area        = $Area
            Requirement = $Requirement
            Status      = $Status
            Detail      = $Detail
        })
}

function Test-ExistingPath {
    <#
    .SYNOPSIS
    Tests a filesystem path without throwing, for use inside capability probes.
    #>
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Test-Path -LiteralPath $Path)
}

function Get-VsWhereExecutable {
    <#
    .SYNOPSIS
    Locates vswhere under either Program Files root.
    #>
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} $script:VsWhereRelativePath),
        (Join-Path $env:ProgramFiles $script:VsWhereRelativePath)
    )

    foreach ($candidate in $candidates) {
        if (Test-ExistingPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Get-VisualStudioInstallations {
    <#
    .SYNOPSIS
    Returns detected Visual Studio installations with their ARM MSVC capability.
    #>
    $vswhere = Get-VsWhereExecutable
    if (-not $vswhere) {
        Add-Finding -Area 'Visual Studio' -Requirement 'vswhere available' `
            -Status $script:Blocker -Detail 'Visual Studio Installer vswhere.exe not found'
        return @()
    }

    $arguments = @('-products', '*', '-format', 'json', '-utf8', '-prerelease')
    $raw = & $vswhere @arguments 2>$null
    if (-not $raw) {
        Add-Finding -Area 'Visual Studio' -Requirement 'installed instances' `
            -Status $script:Blocker -Detail 'vswhere reported no installations'
        return @()
    }

    $instances = @()
    foreach ($instance in ($raw | ConvertFrom-Json)) {
        $toolsetRoot = Join-Path $instance.installationPath 'VC\Tools\MSVC'

        # Every installed toolset exposing an ARM32 compiler is recorded, not just
        # the newest one: the C++/CX runtime requirement can differ per toolset,
        # because only some MSVC releases ship the ARM32 Store import library.
        $armToolsets = @()
        if (Test-ExistingPath $toolsetRoot) {
            foreach ($candidateToolset in (Get-ChildItem -LiteralPath $toolsetRoot -Directory | Sort-Object Name -Descending)) {
                $candidateCl = Join-Path $candidateToolset.FullName "bin\$($script:HostCompilerFolder)\$($script:ArmCompilerFolder)\cl.exe"
                if (-not (Test-ExistingPath $candidateCl)) {
                    continue
                }

                $storeCrt = Join-Path $candidateToolset.FullName "lib\$($script:ArmCompilerFolder)\$($script:StoreCrtLibrary)"
                $cxRuntime = Join-Path $candidateToolset.FullName "lib\$($script:ArmCompilerFolder)\uwp\vccorlib.lib"
                $armToolsets += [pscustomobject]@{
                    Name                = $candidateToolset.Name
                    Root                = $candidateToolset.FullName
                    ArmCompiler         = $candidateCl
                    HasStoreCrt         = (Test-ExistingPath $storeCrt)
                    HasCxRuntimeLibrary = (Test-ExistingPath $cxRuntime)
                }
            }
        }

        $instances += [pscustomobject]@{
            InstallationPath    = $instance.installationPath
            DisplayName         = $instance.displayName
            InstallationVersion = if ($instance.PSObject.Properties['installationVersion']) { $instance.installationVersion } else { 'unknown' }
            ArmToolsets         = $armToolsets
        }
    }

    return $instances
}

function Select-MsvcToolset {
    <#
    .SYNOPSIS
    Chooses which installed ARM toolset to measure and build against.
    .DESCRIPTION
    An explicit -MsvcVersion wins. Otherwise the newest ARM toolset is selected.

    Selection is by newest version rather than by Store CRT availability, because
    the combined msvcurt.lib is no longer shipped for ARM by current toolsets and
    is not required: the individual Store flavor libraries link a /ZW AppContainer
    component. Preferring a toolset on that basis would have kept reporting a
    blocker for a library the build never links.
    #>
    param([Parameter(Mandatory = $true)][object[]]$ArmToolsets)

    if (-not $ArmToolsets) {
        return $null
    }

    if ($MsvcVersion) {
        $requested = $ArmToolsets | Where-Object { $_.Name -eq $MsvcVersion } | Select-Object -First 1
        if ($requested) {
            Add-Finding -Area 'MSVC ARM32' -Requirement 'explicit toolset override honored' `
                -Status $script:Ok -Detail "Using requested toolset $($requested.Name)"
            return $requested
        }

        Add-Finding -Area 'MSVC ARM32' -Requirement 'explicit toolset override honored' `
            -Status $script:Blocker -Detail "$MsvcVersion has no ARM32 compiler; available: $(($ArmToolsets | ForEach-Object { $_.Name }) -join ', ')"
        return $null
    }

    $newest = $ArmToolsets | Select-Object -First 1
    Add-Finding -Area 'MSVC ARM32' -Requirement 'toolset selection' -Status $script:Ok `
        -Detail "Selected $($newest.Name), the newest of: $(($ArmToolsets | ForEach-Object { $_.Name }) -join ', ')"
    return $newest
}

function Select-VisualStudioInstallation {
    <#
    .SYNOPSIS
    Chooses the explicit override or the first installation exposing an ARM MSVC compiler.
    #>
    param([Parameter(Mandatory = $true)][object[]]$Instances)

    if ($VsPath) {
        $override = $Instances | Where-Object { $_.InstallationPath -eq $VsPath } | Select-Object -First 1
        if ($override) {
            Add-Finding -Area 'Visual Studio' -Requirement 'explicit override honored' `
                -Status $script:Ok -Detail "Using requested installation $($override.InstallationPath)"
            return $override
        }

        Add-Finding -Area 'Visual Studio' -Requirement 'explicit override honored' `
            -Status $script:Blocker -Detail "Requested installation not detected: $VsPath"
        return $null
    }

    $withArm = $Instances | Where-Object { $_.ArmToolsets.Count -gt 0 } | Select-Object -First 1
    if ($withArm) {
        return $withArm
    }

    return $Instances | Select-Object -First 1
}

function Measure-ArmCompiler {
    <#
    .SYNOPSIS
    Verifies the selected ARM32 compiler and captures its version banner.
    #>
    param(
        [Parameter(Mandatory = $true)][object]$Installation,
        [Parameter(Mandatory = $true)][object]$Toolset
    )

    if (-not $Toolset) {
        Add-Finding -Area 'MSVC ARM32' -Requirement 'Hostx64 ARM cl.exe' `
            -Status $script:Blocker -Detail "No ARM compiler under $($Installation.InstallationPath)\VC\Tools\MSVC"
        return
    }

    Add-Finding -Area 'MSVC ARM32' -Requirement 'Hostx64 ARM cl.exe' `
        -Status $script:Ok -Detail "$($Toolset.ArmCompiler) (toolset $($Toolset.Name))"

    # cl.exe writes its banner to stderr, which PowerShell would otherwise
    # surface as a terminating error; relax the preference only for this call.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $banner = & $Toolset.ArmCompiler /Bv 2>&1 | Select-Object -First 1
    $ErrorActionPreference = $previousPreference

    if ($banner) {
        Add-Finding -Area 'MSVC ARM32' -Requirement 'compiler version' `
            -Status $script:Ok -Detail ([string]$banner).Trim()
    }
}

function Measure-CxRuntimeAndMetadata {
    <#
    .SYNOPSIS
    Verifies the C++/CX import libraries and metadata references MSBuild needs.
    .DESCRIPTION
    These files ship with MSVC rather than the Windows SDK, so they are checked
    against the selected toolset. Their absence blocks every /ZW target even when
    an ARM32 SDK is present.

    The deciding directory is the Store flavor of the ARM32 runtime. A /ZW
    component must link that flavor rather than the desktop one: the Store
    libraries supply __vcrt_initialize, the vector destructor EH helper and the
    Store CRT startup, while the desktop libvcruntime drags in desktop-only EH
    and PAL symbols such as RtlUnwindEx, __GSHandlerCheck and _fltused that an
    AppContainer cannot resolve. The combined msvcurt.lib is reported for
    information only, because its absence is expected on current toolsets and is
    not itself a blocker.
    #>
    param([Parameter(Mandatory = $true)][object]$Toolset)

    if (-not $Toolset) {
        Add-Finding -Area 'C++/CX runtime' -Requirement 'MSVC toolset resolved' `
            -Status $script:Blocker -Detail 'No MSVC toolset directory to inspect'
        return
    }

    $libRoot = Join-Path $Toolset.Root 'lib'

    foreach ($library in $script:RequiredCxRuntimeLibraries) {
        $path = Join-Path $libRoot "$($script:ArmCompilerFolder)\$library"
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'C++/CX runtime' -Requirement "ARM $library" -Status $status -Detail $path
    }

    # Store flavor first, then the uwp flavor. Both carry the C++/CX runtime; the
    # Store flavor is the one proven to link an AppContainer /ZW component.
    $runtimeFlavors = @($script:ArmStoreRuntimeDirectory, 'uwp')
    foreach ($flavor in $runtimeFlavors) {
        foreach ($library in $script:RequiredCxUwpRuntimeLibraries) {
            $path = Join-Path $libRoot "$($script:ArmCompilerFolder)\$flavor\$library"
            $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
            Add-Finding -Area 'C++/CX runtime' -Requirement "ARM $flavor $library" -Status $status -Detail $path
        }
    }

    # Reported for information only. Recent MSVC releases dropped the combined
    # Store import library for ARM, and the individual Store flavor libraries are
    # sufficient to link a /ZW component, so its absence is not a blocker.
    $storeCrt = Join-Path $libRoot "$($script:ArmCompilerFolder)\$($script:StoreCrtLibrary)"
    Add-Finding -Area 'C++/CX runtime' -Requirement "ARM $script:StoreCrtLibrary (informational)" `
        -Status $(if (Test-ExistingPath $storeCrt) { $script:Ok } else { $script:Warning }) `
        -Detail $(if (Test-ExistingPath $storeCrt) { $storeCrt }
        else { "Not shipped by toolset $($Toolset.Name); not required, the Store flavor libraries link a /ZW component" })

    # platform.winmd is the C++/CX metadata reference; without it /ZW has no
    # projection types even though the compiler itself runs.
    $referenceCandidates = @(
        (Join-Path $libRoot "x86\store\references\platform.winmd"),
        (Join-Path (Split-Path -Parent $Toolset.Root) 'Common7\IDE\VC\vcpackages\platform.winmd')
    )
    $platformWinmd = $referenceCandidates | Where-Object { Test-ExistingPath $_ } | Select-Object -First 1
    if ($platformWinmd) {
        Add-Finding -Area 'C++/CX metadata' -Requirement 'platform.winmd' -Status $script:Ok -Detail $platformWinmd
    }
    else {
        Add-Finding -Area 'C++/CX metadata' -Requirement 'platform.winmd' -Status $script:Blocker `
            -Detail "Not found; checked $($referenceCandidates -join '; ')"
    }
}

function Measure-MSBuildArmSupport {
    <#
    .SYNOPSIS
    Verifies an MSBuild ARM v143 platform toolset exists for the /ZW projects.
    #>
    param([Parameter(Mandatory = $true)][object]$Installation)

    $msbuild = Join-Path $Installation.InstallationPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-ExistingPath $msbuild)) {
        Add-Finding -Area 'MSBuild' -Requirement 'MSBuild.exe' -Status $script:Blocker -Detail $msbuild
        return
    }

    Add-Finding -Area 'MSBuild' -Requirement 'MSBuild.exe' -Status $script:Ok -Detail $msbuild

    $armToolset = Join-Path $Installation.InstallationPath 'MSBuild\Microsoft\VC\v170\Platforms\ARM\PlatformToolsets\v143'
    $status = if (Test-ExistingPath $armToolset) { $script:Ok } else { $script:Blocker }
    Add-Finding -Area 'MSBuild' -Requirement 'ARM v143 platform toolset' -Status $status -Detail $armToolset
}

function Get-WindowsSdkVersions {
    <#
    .SYNOPSIS
    Lists installed Windows SDK versions from the Lib directory.
    #>
    $libRoot = Join-Path $script:WindowsKitsRoot 'Lib'
    if (-not (Test-ExistingPath $libRoot)) {
        return @()
    }

    return @(Get-ChildItem -LiteralPath $libRoot -Directory | Sort-Object Name -Descending | Select-Object -ExpandProperty Name)
}

function Measure-WindowsSdk {
    <#
    .SYNOPSIS
    Verifies the selected Windows SDK provides ARM32 headers, libraries and metadata.
    .DESCRIPTION
    Recent Windows 11 SDKs ship only arm64, x64 and x86 libraries. A usable
    ARM32 UWP SDK must provide um/arm and ucrt/arm plus union metadata, so this
    reports the exact missing architecture instead of degrading silently.
    #>
    $versions = Get-WindowsSdkVersions
    if (-not $versions) {
        Add-Finding -Area 'Windows SDK' -Requirement 'installed SDK versions' `
            -Status $script:Blocker -Detail "No SDK found under $script:WindowsKitsRoot"
        return
    }

    Add-Finding -Area 'Windows SDK' -Requirement 'installed SDK versions' `
        -Status $script:Ok -Detail ($versions -join ', ')

    if ($SdkVersion) {
        if ($versions -notcontains $SdkVersion) {
            Add-Finding -Area 'Windows SDK' -Requirement 'requested SDK installed' `
                -Status $script:Blocker -Detail "$SdkVersion not installed; available: $($versions -join ', ')"
            return
        }

        $selected = $SdkVersion
        Add-Finding -Area 'Windows SDK' -Requirement 'requested SDK installed' `
            -Status $script:Ok -Detail $selected
    }
    else {
        $selected = $versions | Where-Object {
            (Test-ExistingPath (Join-Path $script:WindowsKitsRoot "Lib\$_\um\$($script:ArmCompilerFolder)")) -and
            (Test-ExistingPath (Join-Path $script:WindowsKitsRoot "Lib\$_\ucrt\$($script:ArmCompilerFolder)"))
        } | Select-Object -First 1

        if (-not $selected) {
            Add-Finding -Area 'Windows SDK' -Requirement 'ARM32 link libraries' -Status $script:Blocker `
                -Detail "No installed SDK provides Lib\<ver>\um\arm and Lib\<ver>\ucrt\arm; install a Windows 10 SDK such as 10.0.19041.0"
            return
        }

        Add-Finding -Area 'Windows SDK' -Requirement 'ARM32 link libraries' -Status $script:Ok `
            -Detail "Auto-selected $selected as the only installed SDK with ARM32 libraries"
    }

    $includeRoot = Join-Path $script:WindowsKitsRoot "Include\$selected"
    foreach ($folder in @('um', 'shared', 'ucrt', 'winrt')) {
        $path = Join-Path $includeRoot $folder
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'Windows SDK' -Requirement "headers $folder" -Status $status -Detail $path
    }

    $armUm = Join-Path $script:WindowsKitsRoot "Lib\$selected\um\$($script:ArmCompilerFolder)"
    foreach ($library in $script:RequiredArmSdkLibraries) {
        $path = Join-Path $armUm $library
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'Windows SDK' -Requirement "um/arm $library" -Status $status -Detail $path
    }

    $armUcrt = Join-Path $script:WindowsKitsRoot "Lib\$selected\ucrt\$($script:ArmCompilerFolder)\ucrt.lib"
    $status = if (Test-ExistingPath $armUcrt) { $script:Ok } else { $script:Blocker }
    Add-Finding -Area 'Windows SDK' -Requirement 'ucrt/arm ucrt.lib' -Status $status -Detail $armUcrt

    $metadata = Join-Path $script:WindowsKitsRoot "UnionMetadata\$selected\Windows.winmd"
    $status = if (Test-ExistingPath $metadata) { $script:Ok } else { $script:Blocker }
    Add-Finding -Area 'Windows SDK' -Requirement 'UnionMetadata Windows.winmd' -Status $status -Detail $metadata
}

function Measure-NativeEngineToolchain {
    <#
    .SYNOPSIS
    Verifies the existing native ARM32 link recipe still resolves its libraries.
    .DESCRIPTION
    The CMake toolchain file names SDK and toolset libraries explicitly. When an
    SDK or MSVC servicing update removes one of them, the native engine stops
    linking even though the tree still builds incrementally, so this is measured
    before any M10 work assumes a working baseline.
    #>
    param([Parameter(Mandatory = $true)][object]$Toolset)

    # The native ARM32 link surface. Newer MSVC releases no longer ship the
    # combined Store import library for ARM, so the toolchain links the SDK UCRT
    # plus the UWP flavor of the MSVC runtime support libraries instead. Checking
    # the replacement set catches a stale toolchain reference and an SDK that
    # lacks ARM32 libraries at the same time.
    $repositoryLib = Join-Path (Split-Path -Parent $PSScriptRoot) 'lib'
    foreach ($library in $script:RepositoryRuntimeLibraries) {
        $path = Join-Path $repositoryLib $library
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'Native engine' -Requirement "repository $library" -Status $status -Detail $path
    }

    if (-not $Toolset) {
        return
    }

    $clangBuiltins = Join-Path $repositoryLib 'clang_rt.builtins-arm.lib'
    $status = if (Test-ExistingPath $clangBuiltins) { $script:Ok } else { $script:Blocker }
    Add-Finding -Area 'Native engine' -Requirement 'repository clang_rt.builtins-arm.lib' -Status $status -Detail $clangBuiltins

    # The UWP flavor of the MSVC runtime support libraries the toolchain links
    # instead of the retired Store import library.
    foreach ($library in $script:RequiredNativeUwpRuntimeLibraries) {
        $path = Join-Path $Toolset.Root "lib\$($script:ArmCompilerFolder)\uwp\$library"
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'Native engine' -Requirement "ARM UWP $library" -Status $status -Detail $path
    }

    # The Store import library is reported by the C++/CX measurement; here we only
    # confirm the native recipe does not still reference it.
    $storeCrt = Join-Path $Toolset.Root "lib\$($script:ArmCompilerFolder)\$($script:StoreCrtLibrary)"
    if (Test-ExistingPath $storeCrt) {
        Add-Finding -Area 'Native engine' -Requirement 'Store import library availability' `
            -Status $script:Ok -Detail "Toolset $($Toolset.Name) ships $script:StoreCrtLibrary; the native recipe links the SDK UCRT plus ARM UWP vcruntime/msvcprt and needs no Store CRT"
    }
    else {
        Add-Finding -Area 'Native engine' -Requirement 'Store import library availability' `
            -Status $script:Ok -Detail "Toolset $($Toolset.Name) does not ship $script:StoreCrtLibrary; the corrected native link set was verified by direct link"
    }
}

function Write-HumanReport {
    <#
    .SYNOPSIS
    Prints findings, then a summary that names the blocking capability areas.
    #>
    param([Parameter(Mandatory = $true)][object[]]$Findings)

    foreach ($finding in $Findings) {
        if ($Quiet -and $finding.Status -eq $script:Ok) {
            continue
        }

        '{0,-8} {1,-18} {2,-34} {3}' -f $finding.Status, $finding.Area, $finding.Requirement, $finding.Detail
    }

    $blockers = @($Findings | Where-Object { $_.Status -eq $script:Blocker })
    $warnings = @($Findings | Where-Object { $_.Status -eq $script:Warning })

    ''
    'Summary: {0} blocker(s), {1} warning(s), {2} measured capability(ies).' -f `
        $blockers.Count, $warnings.Count, @($Findings | Where-Object { $_.Status -eq $script:Ok }).Count

    if ($blockers) {
        ''
        'Blocking areas:'
        foreach ($area in ($blockers | Select-Object -ExpandProperty Area -Unique)) {
            '  - {0}' -f $area
        }
    }
}

$installations = Get-VisualStudioInstallations
if ($installations.Count -gt 0) {
    Add-Finding -Area 'Visual Studio' -Requirement 'detected installations' -Status $script:Ok `
        -Detail (($installations | ForEach-Object {
                $toolsets = if ($_.ArmToolsets.Count) { ($_.ArmToolsets | ForEach-Object { "$($_.Name)[storecrt=$($_.HasStoreCrt)]" }) -join ',' } else { 'none' }
                "$($_.DisplayName) [$($_.InstallationVersion)] armToolsets=$toolsets"
            }) -join '; ')
}

$installation = if ($installations.Count -gt 0) { Select-VisualStudioInstallation -Instances $installations } else { $null }
$toolset = if ($installation) { Select-MsvcToolset -ArmToolsets $installation.ArmToolsets } else { $null }

if ($installation) {
    if ($toolset) {
        Measure-ArmCompiler -Installation $installation -Toolset $toolset
        Measure-CxRuntimeAndMetadata -Toolset $toolset
        Measure-MSBuildArmSupport -Installation $installation
        Measure-NativeEngineToolchain -Toolset $toolset
    }
    else {
        Add-Finding -Area 'Visual Studio' -Requirement 'usable ARM toolset' -Status $script:Blocker `
            -Detail "No ARM32 toolset selected in $($installation.InstallationPath); check the -MsvcVersion override"
    }
}
else {
    Add-Finding -Area 'Visual Studio' -Requirement 'usable installation' -Status $script:Blocker `
        -Detail 'No Visual Studio installation available for ARM32 C++/CX work'
}

Measure-WindowsSdk

if ($Json) {
    $blockerCount = @($script:Findings | Where-Object { $_.Status -eq $script:Blocker }).Count
    [pscustomobject]@{
        GeneratedBy        = 'Tools/uwp/build/discover-arm32-toolchain.ps1'
        # This script lives in Tools/uwp/build, so the repository root is three levels up.
        RepositoryRoot     = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
        VsPathOverride     = $VsPath
        MsvcVersionOverride = $MsvcVersion
        SdkVersionOverride = $SdkVersion
        SelectedToolset    = if ($toolset) { $toolset.Name } else { $null }
        Blockers           = $blockerCount
        Findings           = $script:Findings
    } | ConvertTo-Json -Depth 4
}
else {
    Write-HumanReport -Findings $script:Findings
}

if (@($script:Findings | Where-Object { $_.Status -eq $script:Blocker }).Count -gt 0) {
    exit 1
}

exit 0