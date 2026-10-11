#requires -Version 7.0
<# .SYNOPSIS
Tests native frame/service laws and verifies native ARM separation with both compilers.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ComponentDirectory,
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\frame-scheduler-tests')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$identity = Get-Content (Join-Path $ComponentDirectory 'build-identity.json') -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$version = $identity.SdkVersion
$api = Join-Path $repo 'Source\WebKit\UIProcess\API\w10m'
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for scheduler results' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared;$sdk\Include\$version\winrt"
<# .SYNOPSIS
Runs a discovered compiler/linker with an actual exit-code check.
#>
function Invoke-SchedulerTool([string]$executable, [string[]]$arguments) {
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "$executable failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX', "/I$api", "/I$repo\Source\WebKit\Shared\w10m")
    $files = @("$api\rendering\FrameSchedulerW10M.cpp", "$PSScriptRoot\FrameSchedulerTests.cpp")
    $objects = @()
    foreach ($file in $files) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        Invoke-SchedulerTool "$tool\bin\Hostx64\x64\cl.exe" ($common + @("/Fo$object", $file))
        $objects += $object
    }
    Invoke-SchedulerTool "$tool\bin\Hostx64\x64\link.exe" (@('/nologo', '/subsystem:console', "/out:$OutDir\FrameSchedulerTests.exe") + $objects +
        @("/libpath:$tool\lib\x64", "/libpath:$sdk\Lib\$version\ucrt\x64", "/libpath:$sdk\Lib\$version\um\x64",
            'msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'ucrt.lib', 'kernel32.lib'))
    $result = & "$OutDir\FrameSchedulerTests.exe"
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\scheduler-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Native scheduler tests failed: $exitCode" }
    $flags = @($common | Where-Object { $_ -ne '/EHsc' }) + @('/EHs-c-', '/GR-', '/D_HAS_EXCEPTIONS=0', '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP')
    Invoke-SchedulerTool "$tool\bin\Hostx64\arm\cl.exe" ($flags + @("/Fo$OutDir\scheduler-arm-msvc.obj", $files[0]))
    $clang = Join-Path $identity.VsPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
    Invoke-SchedulerTool $clang ($flags + @('--target=armv7-unknown-windows-msvc', "/Fo$OutDir\scheduler-arm-clang.obj", $files[0]))
    'Frame scheduler ARM separation PASS: MSVC + clang-cl, no /ZW'
} finally { $env:INCLUDE = $previousInclude }
