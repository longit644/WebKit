#requires -Version 7.0
<#
.SYNOPSIS
Tests native rendering ownership/metrics and compiles the extraction with both ARM compilers.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ComponentDirectory,
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\rendering-contract-tests')
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
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for rendering results' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared;$sdk\Include\$version\winrt"
<# .SYNOPSIS
Runs a discovered compiler/linker and rejects its actual failure exit code.
#>
function Invoke-RenderingTool([string]$executable, [string[]]$arguments) {
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "$executable failed: $LASTEXITCODE" }
}
try {
    $common = @('/nologo', '/c', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX',
        "/I$api", "/I$repo\Source\WebKit\Shared\w10m")
    $files = @("$api\rendering\SwapChainPresenterW10M.cpp", "$api\rendering\FrameMetricsW10M.cpp", "$PSScriptRoot\RenderingContractTests.cpp")
    $objects = @()
    foreach ($file in $files) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        Invoke-RenderingTool "$tool\bin\Hostx64\x64\cl.exe" ($common + @("/Fo$object", $file))
        $objects += $object
    }
    Invoke-RenderingTool "$tool\bin\Hostx64\x64\link.exe" (@('/nologo', '/subsystem:console', "/out:$OutDir\RenderingContractTests.exe") + $objects +
        @("/libpath:$tool\lib\x64", "/libpath:$sdk\Lib\$version\ucrt\x64", "/libpath:$sdk\Lib\$version\um\x64",
            'msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib', 'ucrt.lib', 'kernel32.lib', 'runtimeobject.lib', 'd3d11.lib', 'dxgi.lib'))
    $result = & "$OutDir\RenderingContractTests.exe"
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\rendering-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Rendering ownership/metrics tests failed: $exitCode" }
    $clang = Join-Path $identity.VsPath 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
    foreach ($compiler in @('msvc', 'clang')) {
        $executable = if ($compiler -eq 'msvc') { "$tool\bin\Hostx64\arm\cl.exe" } else { $clang }
        # Native ARM clang uses the production no-exception ABI; CX remains MSVC-only.
        $flags = @($common | Where-Object { $_ -ne '/EHsc' }) +
            @('/EHs-c-', '/GR-', '/D_HAS_EXCEPTIONS=0', '/DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP', '/D_WIN32_WINNT=0x0A00')
        if ($compiler -eq 'clang') { $flags += '--target=armv7-unknown-windows-msvc' }
        foreach ($file in $files[0..1]) {
            $object = Join-Path $OutDir ("arm-$compiler-" + [IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
            Invoke-RenderingTool $executable ($flags + @("/Fo$object", $file))
        }
    }
    "Rendering ARM compiler separation PASS: MSVC + clang-cl, no /ZW"
} finally { $env:INCLUDE = $previousInclude }
