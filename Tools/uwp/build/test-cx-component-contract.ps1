#requires -Version 7.0
<#
.SYNOPSIS
Tests the new component through its compiler WinMD and exported factory map.
#>
[CmdletBinding()]
param([string]$OutDir = (Join-Path $env:TEMP 'opencode\cx-component-contract'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-arm32-cx-component.ps1') -Architecture x64 -OutDir $OutDir
$identity = Get-Content (Join-Path $OutDir 'build-identity.json') -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$version = $identity.SdkVersion
$cl = Join-Path $tool 'bin\Hostx64\x64\cl.exe'
$link = Join-Path $tool 'bin\Hostx64\x64\link.exe'
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared;$sdk\Include\$version\winrt"
try {
    $source = Join-Path $PSScriptRoot 'CXComponentContractTests.cpp'
    $common = @('/nologo', '/c', '/ZW', '/EHsc', '/MD', '/W4', '/WX', "/AI$tool\lib\x86\store\references",
        "/AI$sdk\UnionMetadata\$version", "/FU$OutDir\WebKit.winmd")
    & $cl @common "/Fo$OutDir\consumer.obj" $source
    if ($LASTEXITCODE -ne 0) { throw 'Generated-metadata consumer compile failed' }
    foreach ($negative in @('TEST_TOKEN_CONSTRUCTION', 'TEST_TOKEN_MUTATION', 'TEST_STATE_MUTATION', 'TEST_EVENT_MUTATION')) {
        $diagnostic = (& $cl @common "/D$negative" "/Fo$OutDir\rejected-$negative.obj" $source 2>&1 | Out-String)
        $expectedError = if ($negative -eq 'TEST_TOKEN_CONSTRUCTION') { 'error C2248:' } else { "error C2039: 'set':" }
        if ($LASTEXITCODE -eq 0 -or $diagnostic -notmatch $expectedError) {
            throw "Negative consumer check did not fail for the expected reason: $negative`n$diagnostic"
        }
        $diagnostic | Set-Content "$OutDir\$negative.txt"
        "PASS: metadata consumer rejects $negative"
    }
    & $link /nologo /subsystem:console /machine:x64 "/out:$OutDir\CXComponentContractTests.exe" "$OutDir\consumer.obj" "/libpath:$tool\lib\x64" `
        "/libpath:$sdk\Lib\$version\um\x64" "/libpath:$sdk\Lib\$version\ucrt\x64" vccorlib.lib msvcprt.lib msvcrt.lib vcruntime.lib ucrt.lib runtimeobject.lib ole32.lib kernel32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Metadata consumer link failed' }
    $result = & "$OutDir\CXComponentContractTests.exe" "$OutDir\WebKit.dll"
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\contract-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Component factory/consumer tests failed: $exitCode" }
} finally {
    $env:INCLUDE = $previousInclude
}
