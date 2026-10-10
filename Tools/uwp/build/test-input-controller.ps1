#requires -Version 7.0
<#
.SYNOPSIS
Tests native input gesture/physics laws and verifies native ARM separation.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ComponentDirectory,
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\input-controller-tests')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$identity = Get-Content (Join-Path $ComponentDirectory 'build-identity.json') -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$version = $identity.SdkVersion
$api = Join-Path $repo 'Source\WebKit\UIProcess\API\uwp'
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for input results' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared;$sdk\Include\$version\winrt"
<#
.SYNOPSIS
Runs a discovered compiler/linker with an actual exit-code check.
#>
function Invoke-InputTool([string]$executable, [string[]]$arguments) {
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "$executable failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX', "/I$api", "/I$repo\Source\WebKit\Shared\uwp")
    $files = @("$api\input\ScrollControllerUWP.cpp", "$api\input\InputAdapterUWP.cpp",
        "$api\native\PageControllerUWP.cpp", "$PSScriptRoot\InputControllerTests.cpp")
    $objects = @()
    foreach ($file in $files) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        Invoke-InputTool "$tool\bin\Hostx64\x64\cl.exe" ($common + @("/Fo$object", $file))
        $objects += $object
    }
    Invoke-InputTool "$tool\bin\Hostx64\x64\link.exe" (@('/nologo', '/subsystem:console', "/out:$OutDir\InputControllerTests.exe") + $objects +
        @("/libpath:$tool\lib\x64", "/libpath:$sdk\Lib\$version\ucrt\x64", "/libpath:$sdk\Lib\$version\um\x64",
            'msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'ucrt.lib', 'kernel32.lib'))
    $result = & "$OutDir\InputControllerTests.exe"
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\input-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Native input tests failed: $exitCode" }
    $flags = @($common | Where-Object { $_ -ne '/EHsc' }) + @('/EHs-c-', '/GR-', '/D_HAS_EXCEPTIONS=0', '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP')
    $clang = Join-Path $identity.VsPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
    foreach ($compiler in @('msvc', 'clang')) {
        $executable = if ($compiler -eq 'msvc') { "$tool\bin\Hostx64\arm\cl.exe" } else { $clang }
        $compilerFlags = $flags
        if ($compiler -eq 'clang') { $compilerFlags += '--target=armv7-unknown-windows-msvc' }
        foreach ($file in $files[0..2]) {
            $object = Join-Path $OutDir ("arm-$compiler-" + [IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
            Invoke-InputTool $executable ($compilerFlags + @("/Fo$object", $file))
        }
    }
    'Input ARM separation PASS: MSVC + clang-cl, no /ZW'
} finally { $env:INCLUDE = $previousInclude }
