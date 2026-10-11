<#
.SYNOPSIS
Copies feature/dependency selections from a rollback cache, excluding stale native flags.
.DESCRIPTION
Reads the existing cache without modifying it. The current environment/toolchain
must supply compiler/header/link flags and the SDK resource compiler on configure.
#>
[CmdletBinding()]
param(
    [string]$SourceCache,
    [string]$OutFile = (Join-Path $env:TEMP 'opencode\m10-fresh-w10m-initial-cache.cmake')
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if (-not $SourceCache) { $SourceCache = Join-Path $repo 'build-arm-uwp-skia\CMakeCache.txt' }
$source = (Resolve-Path -LiteralPath $SourceCache).Path
$output = [IO.Path]::GetFullPath($OutFile)
$oldBuild = [IO.Path]::GetDirectoryName($source)
if ($output.StartsWith($oldBuild + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The output must be outside the preserved build directory'
}
if (-not (Test-Path -LiteralPath (Split-Path -Parent $output))) {
    throw "Output parent directory does not exist: $output"
}
$lines = @('# Feature/dependency selections from the preserved ARM32 tree.',
    '# Native compiler/linker flags and RC must come from the current toolchain.')
$oldNormalized = $oldBuild.Replace('\', '/')
foreach ($line in [IO.File]::ReadAllLines($source)) {
    if ($line -notmatch '^([A-Za-z0-9_]+):(STRING|BOOL|FILEPATH|PATH)=(.*)$') { continue }
    $name, $kind, $value = $Matches[1], $Matches[2], $Matches[3].Replace('\', '/')
    # These cached values overrule *_INIT and would reintroduce SDK 22621/msvcurt.
    if ($name -eq 'CMAKE_RC_COMPILER' -or $name -match 'LINKER_FLAGS|^CMAKE_C_FLAGS|^CMAKE_CXX_FLAGS') { continue }
    if ($value.IndexOf($oldNormalized, [StringComparison]::OrdinalIgnoreCase) -ge 0) { continue }
    # Source tree moved Tools/uwp -> Tools/w10m; remap surviving source paths.
    $value = $value.Replace('Tools/uwp/', 'Tools/w10m/')
    $value = $value.Replace('"', '\"').Replace('$', '\$')
    $lines += 'set({0} "{1}" CACHE {2} "")' -f $name, $value, $kind
}
[IO.File]::WriteAllLines($output, $lines)
"Initial cache: $output ($($lines.Count - 2) entries); preserved tree unchanged"
