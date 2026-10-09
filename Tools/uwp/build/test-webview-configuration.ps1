#requires -Version 7.0
<#
.SYNOPSIS
Tests copied M10 options and pre-page validation with isolated local fixtures.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\webview-configuration'),
    [string]$FontDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$api = Join-Path $repo 'Source\WebKit\UIProcess\API\uwp'
if (-not $FontDirectory) { $FontDirectory = Join-Path $repo 'build-arm-minibrowser\package-skia\Fonts' }
if (-not (Test-Path -LiteralPath (Split-Path -Parent $OutDir))) { throw "Output parent does not exist: $OutDir" }
if (Test-Path -LiteralPath $OutDir) { throw 'Use a fresh -OutDir for isolated configuration fixtures' }
foreach ($font in @('DejaVuSans.ttf', 'DejaVuSans-Bold.ttf')) {
    if (-not (Test-Path -LiteralPath (Join-Path $FontDirectory $font))) { throw "Required fixture font missing: $font; specify -FontDirectory" }
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
& (Join-Path $PSScriptRoot 'build-arm32-cx-component.ps1') -OutDir (Join-Path $OutDir 'arm')
$identity = Get-Content (Join-Path $OutDir 'arm\build-identity.json') -Raw | ConvertFrom-Json
$tool = $identity.ToolsetRoot
$sdk = $identity.SdkRoot
$version = $identity.SdkVersion
$fixture = Join-Path $OutDir 'fixture'
New-Item -ItemType Directory -Path "$fixture\Fonts", "$fixture\FontsIncomplete" -Force | Out-Null
Copy-Item (Join-Path $repo 'Tools\uwp\NavigationTests\tls\fixture-ca.pem') "$fixture\cacert.pem"
Copy-Item (Join-Path $FontDirectory 'DejaVuSans.ttf'), (Join-Path $FontDirectory 'DejaVuSans-Bold.ttf') "$fixture\Fonts"
Copy-Item (Join-Path $FontDirectory 'DejaVuSans.ttf') "$fixture\FontsIncomplete"
[IO.File]::WriteAllBytes("$fixture\empty.pem", [byte[]]@())
[IO.File]::WriteAllText("$fixture\malformed.pem", "-----BEGIN CERTIFICATE-----`ninvalid`n-----END CERTIFICATE-----")
[IO.File]::WriteAllText("$fixture\not-certificates.pem", 'not a certificate')
[IO.File]::WriteAllBytes("$fixture\nul.pem", ([IO.File]::ReadAllBytes("$fixture\cacert.pem") + [byte[]]@(0)))
$caHash = (Get-FileHash "$fixture\cacert.pem" -Algorithm SHA256).Hash.ToLowerInvariant()
$hash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
try {
    foreach ($font in @('DejaVuSans-Bold.ttf', 'DejaVuSans.ttf')) {
        $hash.AppendData([Text.Encoding]::UTF8.GetBytes($font))
        $hash.AppendData([IO.File]::ReadAllBytes((Join-Path $fixture "Fonts\$font")))
    }
    $fontHash = [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
} finally { $hash.Dispose() }
$previousInclude = $env:INCLUDE
$env:INCLUDE = "$tool\include;$sdk\Include\$version\ucrt;$sdk\Include\$version\um;$sdk\Include\$version\shared;$sdk\Include\$version\winrt"
try {
    $cl = Join-Path $tool 'bin\Hostx64\x64\cl.exe'
    $link = Join-Path $tool 'bin\Hostx64\x64\link.exe'
    $common = @('/nologo', '/c', '/EHsc', '/MD', '/std:c++17', '/W4', '/WX', '/DNOMINMAX', "/I$api", "/I$repo\Source\WebKit\Shared\uwp")
    $objects = @()
    foreach ($file in @("$api\native\WebViewConfigurationUWP.cpp", "$api\WebKitWebViewOptions.cpp", "$PSScriptRoot\WebViewConfigurationTests.cpp")) {
        $object = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($file) + '.obj')
        $arguments = $common + @("/Fo$object", $file)
        if ($file -notmatch 'WebViewConfigurationUWP\.cpp$') {
            $arguments += @('/ZW', "/AI$tool\lib\x86\store\references", "/AI$sdk\UnionMetadata\$version")
        }
        & $cl @arguments
        if ($LASTEXITCODE -ne 0) { throw "Configuration test compile failed: $file" }
        $objects += $object
    }
    $arguments = @('/nologo', '/subsystem:console', '/machine:x64', "/out:$OutDir\WebViewConfigurationTests.exe",
        "/libpath:$tool\lib\x64", "/libpath:$sdk\Lib\$version\um\x64", "/libpath:$sdk\Lib\$version\ucrt\x64") + $objects +
        @('vccorlib.lib', 'msvcprt.lib', 'msvcrt.lib', 'vcruntime.lib', 'ucrt.lib', 'runtimeobject.lib', 'ole32.lib', 'kernel32.lib', 'bcrypt.lib', 'crypt32.lib')
    & $link @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Configuration test link failed' }
    $result = & "$OutDir\WebViewConfigurationTests.exe" $fixture $caHash $fontHash
    $exitCode = $LASTEXITCODE
    $result | Set-Content "$OutDir\configuration-results.txt"
    $result
    if ($exitCode -ne 0) { throw "Configuration tests failed: $exitCode" }
} finally { $env:INCLUDE = $previousInclude }
