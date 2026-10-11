#requires -Version 7.0
<#
.SYNOPSIS
Tests the native Source state using tool discovery from a built component.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ComponentDirectory,
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\navigation-state-tests')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$identity = Get-Content (Join-Path $ComponentDirectory 'build-identity.json') -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$version = $identity.SdkVersion
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for native state results' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared"
try {
    $arguments = @('/nologo', '/c', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX',
        "/I$repo\Source\WebKit\UIProcess\API\w10m\native", "/I$repo\Source\WebKit\Shared\w10m")
    $objects = @()
    foreach ($file in @("$repo\Source\WebKit\UIProcess\API\w10m\native\NavigationStateW10M.cpp", "$PSScriptRoot\NavigationStateTests.cpp")) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        & "$tool\bin\Hostx64\x64\cl.exe" @arguments "/Fo$object" $file
        if ($LASTEXITCODE -ne 0) { throw 'Native state test compile failed' }
        $objects += $object
    }
    & "$tool\bin\Hostx64\x64\link.exe" /nologo /subsystem:console "/out:$OutDir\NavigationStateTests.exe" @objects `
        "/libpath:$tool\lib\x64" "/libpath:$sdk\Lib\$version\ucrt\x64" "/libpath:$sdk\Lib\$version\um\x64" msvcrt.lib msvcprt.lib vcruntime.lib ucrt.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native state test link failed' }
    $result = & "$OutDir\NavigationStateTests.exe"
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\navigation-state-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Native state tests failed: $exitCode" }
} finally { $env:INCLUDE = $previousInclude }
