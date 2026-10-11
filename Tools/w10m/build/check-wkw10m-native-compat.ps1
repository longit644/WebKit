<#
.SYNOPSIS
Compiles native compatibility ABI checks with both ARM32 boundary compilers.
#>
[CmdletBinding()]
param([string]$OutDir = (Join-Path $env:TEMP 'opencode\native-compat-check'))
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
. (Join-Path $repo 'Tools\w10m\arm32-w10m-env.ps1')
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) {
    throw "Output parent directory does not exist: $OutDir"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$source = Join-Path $PSScriptRoot 'wkw10m-native-compat-check.cpp'
$include = Join-Path $repo 'Tools\w10m\toolchain'
$common = @('/nologo', '/c', '/W4', '/WX', '/EHs-c-', '/GR-', '/MD',
    '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00', ('/I' + $include))
$compilers = @(
    @{ Name = 'native'; Path = $env:WK_W10M_CLANG_CL; Extra = @('--target=armv7-unknown-windows-msvc') },
    @{ Name = 'MSVC'; Path = (Join-Path $env:WK_W10M_TOOLSET_ROOT 'bin\Hostx64\arm\cl.exe'); Extra = @() }
)
foreach ($compiler in $compilers) {
    $object = Join-Path $OutDir ("native-compat-" + $compiler.Name + '.obj')
    $arguments = $common + $compiler.Extra + @(('/Fo' + $object), $source)
    & $compiler.Path @arguments
    if ($LASTEXITCODE -ne 0) { throw "$($compiler.Name) compatibility ABI check failed" }
    "$($compiler.Name): compatibility layout and signatures PASS"
}
