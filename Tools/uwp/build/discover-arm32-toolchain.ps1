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
$script:RequiredCxUwpRuntimeLibraries = @('msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'vccorlib.lib')

# Repository-owned runtime support that the native ARM32 link recipe depends on.
$script:RepositoryRuntimeLibraries = @('arm-crtstart.lib')

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
        $armCl = $null
        $toolset = $null
        if (Test-ExistingPath $toolsetRoot) {
            foreach ($candidateToolset in (Get-ChildItem -LiteralPath $toolsetRoot -Directory | Sort-Object Name -Descending)) {
                $candidateCl = Join-Path $candidateToolset.FullName "bin\$($script:HostCompilerFolder)\$($script:ArmCompilerFolder)\cl.exe"
                if (Test-ExistingPath $candidateCl) {
                    $toolset = $candidateToolset
                    $armCl = $candidateCl
                    break
                }
            }
        }

        $instances += [pscustomobject]@{
            InstallationPath    = $instance.installationPath
            DisplayName         = $instance.displayName
            InstallationVersion = if ($instance.PSObject.Properties['installationVersion']) { $instance.installationVersion } else { 'unknown' }
            Toolset             = if ($toolset) { $toolset.Name } else { $null }
            ArmCompiler         = $armCl
            MsvcRoot            = if ($toolset) { $toolset.FullName } else { $null }
        }
    }

    return $instances
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

    $withArm = $Instances | Where-Object { $_.ArmCompiler } | Select-Object -First 1
    if ($withArm) {
        return $withArm
    }

    return $Instances | Select-Object -First 1
}

function Measure-ArmCompiler {
    <#
    .SYNOPSIS
    Verifies the ARM32 compiler and captures its reported version banner.
    #>
    param([Parameter(Mandatory = $true)][object]$Installation)

    if (-not $Installation.ArmCompiler) {
        Add-Finding -Area 'MSVC ARM32' -Requirement 'Hostx64 ARM cl.exe' `
            -Status $script:Blocker -Detail "No ARM compiler under $($Installation.InstallationPath)\VC\Tools\MSVC"
        return
    }

    Add-Finding -Area 'MSVC ARM32' -Requirement 'Hostx64 ARM cl.exe' `
        -Status $script:Ok -Detail $Installation.ArmCompiler

    # cl.exe writes its banner to stderr, which PowerShell would otherwise
    # surface as a terminating error; relax the preference only for this call.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $banner = & $Installation.ArmCompiler /Bv 2>&1 | Select-Object -First 1
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
    #>
    param([Parameter(Mandatory = $true)][object]$Installation)

    if (-not $Installation.MsvcRoot) {
        Add-Finding -Area 'C++/CX runtime' -Requirement 'MSVC toolset resolved' `
            -Status $script:Blocker -Detail 'No MSVC toolset directory to inspect'
        return
    }

    $libRoot = Join-Path $Installation.MsvcRoot 'lib'

    foreach ($library in $script:RequiredCxRuntimeLibraries) {
        $path = Join-Path $libRoot "$($script:ArmCompilerFolder)\$library"
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'C++/CX runtime' -Requirement "ARM $library" -Status $status -Detail $path
    }

    foreach ($library in $script:RequiredCxUwpRuntimeLibraries) {
        $path = Join-Path $libRoot "$($script:ArmCompilerFolder)\uwp\$library"
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'C++/CX runtime' -Requirement "ARM UWP $library" -Status $status -Detail $path
    }

    # platform.winmd is the C++/CX metadata reference; without it /ZW has no
    # projection types even though the compiler itself runs.
    $referenceCandidates = @(
        (Join-Path $libRoot "x86\store\references\platform.winmd"),
        (Join-Path (Split-Path -Parent $Installation.InstallationPath) 'Common7\IDE\VC\vcpackages\platform.winmd')
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
    param([Parameter(Mandatory = $true)][object]$Installation)

    $repositoryLib = Join-Path (Split-Path -Parent $PSScriptRoot) 'lib'
    foreach ($library in $script:RepositoryRuntimeLibraries) {
        $path = Join-Path $repositoryLib $library
        $status = if (Test-ExistingPath $path) { $script:Ok } else { $script:Blocker }
        Add-Finding -Area 'Native engine' -Requirement "repository $library" -Status $status -Detail $path
    }

    if (-not $Installation.MsvcRoot) {
        return
    }

    # The ARM32 Store CRT import library the toolchain file links directly.
    $storeCrt = Join-Path $Installation.MsvcRoot "lib\$($script:ArmCompilerFolder)\msvcurt.lib"
    $status = if (Test-ExistingPath $storeCrt) { $script:Ok } else { $script:Blocker }
    Add-Finding -Area 'Native engine' -Requirement 'ARM msvcurt.lib named by toolchain' -Status $status -Detail $storeCrt
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
        -Detail (($installations | ForEach-Object { "$($_.DisplayName) [$($_.InstallationVersion)] toolset=$($_.Toolset) arm=$([bool]$_.ArmCompiler)" }) -join '; ')
}

$selected = if ($installations.Count -gt 0) { Select-VisualStudioInstallation -Instances $installations } else { $null }

if ($selected) {
    Measure-ArmCompiler -Installation $selected
    Measure-CxRuntimeAndMetadata -Installation $selected
    Measure-MSBuildArmSupport -Installation $selected
    Measure-NativeEngineToolchain -Installation $selected
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
        RepositoryRoot     = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
        VsPathOverride     = $VsPath
        SdkVersionOverride = $SdkVersion
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