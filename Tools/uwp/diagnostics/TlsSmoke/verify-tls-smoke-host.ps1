<#
.SYNOPSIS
Host-side proof that the migrated TlsSmoke payload is behavior-identical (M10 task 8.7).
.DESCRIPTION
Compares the new payload against expectations inventoried from the committed
HEAD sources, asserts fail-closed TLS structure and warning-policy
independence statically, then builds the real payload for the host (MinGW x64
against real libcurl/OpenSSL) and runs the full phase sequence twice: with an
absent CA bundle (must fail closed) and with a Windows-store-exported root
(success path when the network allows). All outputs live under a fresh -OutDir
(default %TEMP%\opencode\tls-hostproof). Nothing here touches the device.
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $env:TEMP 'opencode\tls-hostproof')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$source = Join-Path $repo 'Tools\uwp\diagnostics\TlsSmoke'
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (Test-Path -LiteralPath $OutDir) { throw "Use a fresh -OutDir; found stale output: $OutDir" }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$failures = New-Object System.Collections.ArrayList
function Assert-True([bool]$condition, [string]$name, [string]$detail = '') {
    if ($condition) { Write-Host "    PASS: $name" }
    else {
        Write-Host "    FAIL: $name $detail"
        [void]$failures.Add($name)
    }
}

function Get-Markers([string]$text) {
    return @([regex]::Matches($text, 'logResult\("([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
}

Write-Host '--- 1. inventory from committed HEAD ---'
Push-Location $repo
try {
    $headCpp = (& git show 'HEAD:Tools/uwp/TlsSmoke/TlsSmoke.cpp' 2>&1) -join "`n"
    $headReadCode = $LASTEXITCODE
}
finally { Pop-Location }
if ($headReadCode -ne 0 -or -not $headCpp -or $headCpp -match '(?m)^fatal:') {
    throw 'Could not read committed baseline Tools/uwp/TlsSmoke/TlsSmoke.cpp; refusing to continue without the semantic reference'
}
$headMarkers = Get-Markers $headCpp
if ($headMarkers.Count -eq 0 -or $headCpp -notmatch '(?s)enum TlsProbeLogBoolean\s*\{' -or
    $headCpp -notmatch '(?m)^constexpr .+kCaBundleFilename') {
    throw 'Committed baseline is incomplete; refusing to treat a missing/invalid source as an empty semantic reference'
}
$newCpp = [IO.File]::ReadAllText((Join-Path $source 'TlsProbe.cpp'))
$newMarkers = Get-Markers $newCpp
Write-Host "    HEAD markers: $($headMarkers.Count); new payload markers: $($newMarkers.Count)"

# The payload keeps every phase marker verbatim; only the host-owned emission
# sites moved (tls-startup/tls-package-path into C#, tls-ca-present stays).
$headPhase = @($headMarkers | Where-Object { $_ -ne 'tls-startup' -and $_ -ne 'tls-package-path' })
Assert-True (($newMarkers -join '|') -eq ($headPhase -join '|')) 'payload marker names and phase ordering unchanged' `
    "HEAD: $($headPhase -join ',')"
$headEnums = [regex]::Matches($headCpp, '(?s)enum TlsProbe\w+ \{.*?\};') | ForEach-Object { $_.Value }
$newEnums = [regex]::Matches($newCpp, '(?s)enum TlsProbe\w+ \{.*?\};') | ForEach-Object { $_.Value }
Assert-True (($newEnums -join "`n") -eq ($headEnums -join "`n")) 'probe enums numerically identical'
$headConsts = @([regex]::Matches($headCpp, '(?m)^constexpr .*$') | ForEach-Object { $_.Value })
$newConsts = @([regex]::Matches($newCpp, '(?m)^constexpr .*$') | ForEach-Object { $_.Value })
$dropped = @($headConsts | Where-Object { $newConsts -notcontains $_ })
$added = @($newConsts | Where-Object { $headConsts -notcontains $_ })
Assert-True (($dropped.Count -eq 1) -and ($dropped[0] -match 'kCaBundleFilename') -and ($added.Count -eq 0)) `
    'only the CA filename constant moved to the C# host' "dropped: $($dropped -join '; ')"
$interop = [IO.File]::ReadAllText((Join-Path $source 'TlsProbeInterop.cs'))
$appCs = [IO.File]::ReadAllText((Join-Path $source 'App.xaml.cs'))
Assert-True ($interop -match 'CaBundleFilename = "cacert.pem"') 'C# owns the cacert.pem filename'
Assert-True ($interop -match 'ReportFilename = "tls-smoke.txt"') 'C# owns the tls-smoke.txt filename'
Assert-True ($interop -match 'StartupStage = "tls-startup"') 'C# emits tls-startup'
Assert-True ($interop -match 'PackagePathStage = "tls-package-path"') 'C# emits tls-package-path'

Write-Host '--- 2. fail-closed structure ---'
Assert-True ($newCpp -match 'CURLOPT_SSL_VERIFYPEER, kCurlVerifyPeer') 'verified phase enables peer verification'
Assert-True ($newCpp -match 'CURLOPT_SSL_VERIFYHOST, kCurlVerifyHost') 'verified phase enables strict host verification'
Assert-True ($newCpp -match 'CURLOPT_CAINFO, caPath') 'verified phase pins the bundle path'
Assert-True ($newCpp -match 'CURLOPT_SSL_VERIFYPEER, kCurlDisableVerification') 'insecure phase is explicitly unverified (peer)'
Assert-True ($newCpp -match 'CURLOPT_SSL_VERIFYHOST, kCurlDisableVerification') 'insecure phase is explicitly unverified (host)'
Assert-True ($newCpp -match 'verified = runPhase\(kHttpsProbeURL, true, caPath\)') 'verified outcome runs verified'
Assert-True ($newCpp -match 'insecure = runPhase\(kHttpsProbeURL, false, nullptr\)') 'insecure outcome runs unverified'
$stripped = (($newCpp -split "`n" | Where-Object { $_.Trim() -notmatch '^//' } | ForEach-Object { $_ -replace '"[^"]*"', '""' }) -join "`n")
$verifiedUses = @([regex]::Matches($stripped, '\bverified\b')).Count
$insecureUses = @([regex]::Matches($stripped, '\binsecure\b')).Count
Assert-True ($verifiedUses -eq 4) 'verified result is recorded once and never reassigned' "uses: $verifiedUses"
Assert-True ($insecureUses -eq 4) 'insecure result is recorded once and never feeds verification' "uses: $insecureUses"
Assert-True ($newCpp -match 'kCurlHandleCreationFailed = -100') 'handle failure stays distinct from any CURLcode'

Write-Host '--- 3. warning-policy independence ---'
$codeOnly = ($interop -split "`n" | Where-Object { $_ -notmatch '^\s*///' }) -join "`n"
$codeOnly += "`n" + (($appCs -split "`n" | Where-Object { $_ -notmatch '^\s*///' }) -join "`n")
$policyApis = @('SslStream', 'ServicePointManager', 'ServerCertificate', 'IgnoreSSL', 'VERIFYHOST',
    'VERIFYPEER', 'HttpClientHandler', 'DangerousAccept', 'CertificateValidation', 'CURLOPT')
$found = @($policyApis | Where-Object { $codeOnly -match $_ })
Assert-True ($found.Count -eq 0) 'C# host sets no TLS policy at all' "found: $($found -join ', ')"

Write-Host '--- 4. host build of the real payload ---'
$mingw = 'C:\Strawberry\c\bin'
$curlInc = 'C:\vcpkg\installed\x64-windows-webkit\include'
$sslInc = 'C:\Strawberry\c\include'
$curlDll = Get-ChildItem 'C:\vcpkg\installed\x64-windows-webkit\bin' -Filter 'libcurl*.dll' | Select-Object -First 1
foreach ($required in @("$mingw\gcc.exe", "$mingw\g++.exe", "$mingw\gendef.exe", "$mingw\dlltool.exe",
        "$curlInc\curl\curl.h", "$sslInc\openssl\ssl.h")) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Host proof input missing: $required" }
}
if (-not $curlDll) { throw 'No host libcurl DLL in x64-windows-webkit\bin' }
$cryptoDll = Get-ChildItem "$mingw\..\bin" -Filter 'libcrypto-*.dll' | Select-Object -First 1
$sslDll = Get-ChildItem "$mingw\..\bin" -Filter 'libssl-*.dll' | Select-Object -First 1
if (-not $cryptoDll -or -not $sslDll) {
    $cryptoDll = Get-ChildItem 'C:\Strawberry\c\bin' -Filter 'libcrypto-*.dll' | Select-Object -First 1
    $sslDll = Get-ChildItem 'C:\Strawberry\c\bin' -Filter 'libssl-*.dll' | Select-Object -First 1
}
if (-not $cryptoDll -or -not $sslDll) { throw 'No host OpenSSL DLLs' }
$gen = Join-Path $OutDir 'genlib'
New-Item -ItemType Directory -Path $gen -Force | Out-Null
Push-Location $gen
try {
    foreach ($dll in @($curlDll.FullName, $cryptoDll.FullName, $sslDll.FullName)) {
        $base = [IO.Path]::GetFileNameWithoutExtension($dll)
        & "$mingw\gendef.exe" "$dll" | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $gen "$base.def"))) { throw "gendef produced no def for $base" }
        & "$mingw\dlltool.exe" -d (Join-Path $gen "$base.def") -l (Join-Path $gen "$base.a") | Out-Null
    }
}
finally { Pop-Location }
$objects = @()
foreach ($tu in @('TlsProbe.cpp', 'TlsHostProof.cpp')) {
    $obj = Join-Path $OutDir "$tu.o"
    # -Wno-cpp silences only MinGW's own winsock2-include-order note, which the
    # MSVC/UWP build does not emit; payload code warnings would still fail here.
    & "$mingw\g++.exe" '-std=c++17' '-O1' '-Wno-cpp' '-D_WIN32_WINNT=0x0A00' '-DNOMINMAX' '-D_WINSOCKAPI_=' `
        "-I$source" "-I$curlInc" "-I$sslInc" '-c' (Join-Path $source $tu) '-o' $obj 2>&1 |
        ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) { throw "Host compile failed: $tu" }
    $objects += $obj
}
$proofExe = Join-Path $OutDir 'TlsHostProof.exe'
& "$mingw\g++.exe" $objects '-o' $proofExe ("-L$gen") '-lcurl' `
    ("-l:" + [IO.Path]::GetFileNameWithoutExtension($cryptoDll.FullName) + '.a') `
    ("-l:" + [IO.Path]::GetFileNameWithoutExtension($sslDll.FullName) + '.a') `
    '-lbcrypt' '-lws2_32' 2>&1 | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -ne 0) { throw 'Host link failed' }
Write-Host '    host proof binary links against real libcurl/OpenSSL'
Copy-Item $curlDll.FullName, $cryptoDll.FullName, $sslDll.FullName $OutDir
# Transitive host-only closure: curl's zlib plus the MinGW runtime.
$curlBin = Split-Path -Parent $curlDll.FullName
foreach ($extra in @((Join-Path $curlBin 'z.dll'), (Join-Path $mingw 'libgcc_s_seh-1.dll'), (Join-Path $mingw 'libstdc++-6.dll'))) {
    if (-not (Test-Path -LiteralPath $extra)) { throw "Host proof dependency missing: $extra" }
    Copy-Item $extra $OutDir
}

Write-Host '--- 5. fail-closed run (absent bundle) ---'
$runAbsent = Join-Path $OutDir 'run-absent'
New-Item -ItemType Directory -Path $runAbsent -Force | Out-Null
& $proofExe (Join-Path $runAbsent 'tls-smoke.txt') '-' | ForEach-Object { Write-Host "    $_" }
$absentLines = Get-Content (Join-Path $runAbsent 'tls-smoke.txt')
$absentStages = @($absentLines | ForEach-Object { ($_ -split ':')[0] })
# Source order is runPhase (enter/exit literals) then runTlsProbes (crypto,
# outcomes, tls-done); the exports hold the two ca-present branches last.
$cryptoStart = [array]::IndexOf($headPhase, 'tls-bcrypt-enter')
$cryptoEnd = [array]::IndexOf($headPhase, 'tls-global-init-exit')
$doneIndex = [array]::IndexOf($headPhase, 'tls-done')
Assert-True (($cryptoEnd - $cryptoStart) -eq 25) 'crypto block is 26 contiguous lines'
$easy = @($headPhase[($cryptoStart - 4)..($cryptoStart - 1)])
Assert-True (($easy -join ',') -eq 'tls-easy-init-enter,tls-easy-init-exit,tls-perform-enter,tls-perform-exit') 'phase literal block shape'
Assert-True (($doneIndex - $cryptoEnd) -eq 10) 'outcomes then tls-done follow the crypto block'
$caLiterals = @($headPhase | Where-Object { $_ -eq 'tls-ca-present' })
Assert-True ($caLiterals.Count -eq 2) 'ca-present has one emission with two source branches'
$crypto = @($headPhase[$cryptoStart..$cryptoEnd])
$plain = @($headPhase[($cryptoEnd + 1)..($cryptoEnd + 3)])
$insecure = @($headPhase[($cryptoEnd + 4)..($cryptoEnd + 6)])
$verified = @($headPhase[($cryptoEnd + 7)..($cryptoEnd + 9)])
$expectedStages = @('tls-startup', 'tls-ca-present') + $crypto + $easy + $plain + $easy + $insecure + $easy + $verified + @('tls-done')
Assert-True (($absentStages -join '|') -eq ($expectedStages -join '|')) 'absent-bundle report order matches inventory' `
    "got $($absentStages.Count) stages, want $($expectedStages.Count)"
function Get-Value([string[]]$lines, [string]$stage) {
    $line = @($lines | Where-Object { $_ -like "$stage`:*" })[0]
    return [Convert]::ToUInt32(($line -split '0x')[1], 16)
}
Assert-True ((Get-Value $absentLines 'tls-startup') -eq 0) 'tls-startup is S_OK'
Assert-True ((Get-Value $absentLines 'tls-ca-present') -eq 0) 'absent bundle logs ca-present false'
Assert-True ((Get-Value $absentLines 'tls-verified-curl-code') -ne 0) 'verified phase fails closed without a bundle'
Assert-True ((Get-Value $absentLines 'tls-verified-bytes') -eq 0) 'failed verification yields zero bytes'
Assert-True ((Get-Value $absentLines 'tls-verified-http-code') -eq 0) 'failed verification yields no HTTP status'
Assert-True ((Get-Value $absentLines 'tls-done') -eq 0) 'report always terminates in tls-done'
foreach ($line in $absentLines) {
    if ($line -notmatch '^[a-z-]+: 0x[0-9a-f]{8}\r?$') {
        Assert-True $false 'every line keeps the report shape' $line
        break
    }
}
Write-Host '    report shape holds for every line'

Write-Host '--- 6. verification run (Windows root bundle) ---'
$runPresent = Join-Path $OutDir 'run-present'
New-Item -ItemType Directory -Path $runPresent -Force | Out-Null
$root = Get-ChildItem 'Cert:\LocalMachine\Root' | Where-Object { $_.Subject -like '*ISRG Root X1*' } | Select-Object -First 1
if (-not $root) { $root = Get-ChildItem 'Cert:\LocalMachine\Root' | Where-Object { $_.Subject -like '*Baltimore*' } | Select-Object -First 1 }
if ($root) {
    $der = $root.Export('Cert')
    $pem = "-----BEGIN CERTIFICATE-----`r`n" +
        ([Convert]::ToBase64String($der, 'InsertLineBreaks')) +
        "`r`n-----END CERTIFICATE-----`r`n"
    Set-Content (Join-Path $runPresent 'test-ca.pem') $pem -Encoding Ascii
    & $proofExe (Join-Path $runPresent 'tls-smoke.txt') (Join-Path $runPresent 'test-ca.pem') |
        ForEach-Object { Write-Host "    $_" }
    $presentLines = Get-Content (Join-Path $runPresent 'tls-smoke.txt')
    $presentStages = @($presentLines | ForEach-Object { ($_ -split ':')[0] })
    Assert-True (($presentStages -join '|') -eq ($expectedStages -join '|')) 'present-bundle report order matches inventory'
    Assert-True ((Get-Value $presentLines 'tls-ca-present') -eq 1) 'present bundle logs ca-present true'
    $verifiedCode = Get-Value $presentLines 'tls-verified-curl-code'
    Write-Host "    tls-verified-curl-code: 0x$($verifiedCode.ToString('x8')) (network-dependent; ordering/fail-closed shape is the asserted invariant)"
}
else { Write-Host '    no suitable Windows root found; success-path run skipped (ordering already proven)' }

Write-Host ''
$report = Join-Path $OutDir 'tls-hostproof-report.txt'
"failures=$($failures.Count)" | Set-Content $report
$failures | ForEach-Object { Add-Content $report $_ }
if ($failures.Count -gt 0) { throw "Host proof failed: $($failures -join '; ')" }
Write-Host 'TlsSmoke host proof passed.'
