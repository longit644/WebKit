#requires -Version 7.0
<#
.SYNOPSIS
Host-runnable checks for BrowserPolicy URI, template, escaping, and warning-token rules.
Compiles only the POLICY_RULES_ONLY section of BrowserPolicy.cs with the desktop C# compiler.
#>
[CmdletBinding()]
param(
    [string]$MsBuildPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'Services\BrowserPolicy.cs'
$csc = Join-Path $MsBuildPath 'Roslyn\csc.exe'
foreach ($required in @($source, $csc)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required policy-check input missing: $required" }
}
$work = Join-Path ([IO.Path]::GetTempPath()) ('opencode\browser-policy-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    $driver = @'
using System;
using System.IO;
using WebKitMiniBrowser;
/// <summary>Host-only executable that validates the pure BrowserPolicy rules.</summary>
sealed class BrowserPolicyDriver
{
    /// <summary>Number of policy assertions evaluated.</summary>
    static int checks, failures;
    /// <summary>Evaluates one named assertion and retains every failure.</summary>
    /// <param name="value">Whether the expected rule result was observed.</param>
    /// <param name="name">Human-readable assertion name.</param>
    static void Check(bool value, string name) { checks++; if (!value) { failures++; Console.WriteLine("FAIL: " + name); } }
    /// <summary>Runs the host-only policy-rule assertions and returns a fail-closed exit code.</summary>
    /// <returns>Zero only when every assertion passes.</returns>
    static int Main()
    {
        Check(BrowserPolicyRules.IsAllowedUri(new Uri("https://example.test/path")), "https URI");
        Check(BrowserPolicyRules.IsAllowedUri(new Uri("HTTP://example.test")), "case-insensitive HTTP URI");
        Check(!BrowserPolicyRules.IsAllowedUri(new Uri("file:///private")), "file URI rejected");
        Check(!BrowserPolicyRules.IsAllowedUri(new Uri("javascript:alert(1)")), "script URI rejected");
        Check(!BrowserPolicyRules.IsAllowedUri(null), "null URI rejected");
        Check(BrowserPolicyRules.EscapeHtml("<&>\"'") == "&lt;&amp;&gt;&quot;&#39;", "HTML escaping");
        string package = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".html");
        string over = package + ".override";
        string validPackage = "<html>{{title}} {{uri}}</html>";
        try {
            File.WriteAllText(package, validPackage);
            File.WriteAllText(over, "<html>override {{title}} {{uri}}</html>");
            Check(BrowserPolicyRules.LoadTemplate(over, package).StartsWith("<html>override", StringComparison.Ordinal), "override wins");
            File.WriteAllText(over, "not a valid template");
            Check(BrowserPolicyRules.LoadTemplate(over, package) == validPackage, "malformed override falls back to package");
            File.Delete(over);
            Check(BrowserPolicyRules.LoadTemplate(over, package) == validPackage, "missing override falls back to package");
            File.Delete(package);
            Check(BrowserPolicyRules.LoadTemplate(over, package).IndexOf("Unable to display", StringComparison.Ordinal) >= 0, "missing package uses safe built-in");
        } finally { if (File.Exists(package)) File.Delete(package); if (File.Exists(over)) File.Delete(over); }
        var state = new WarningEligibility();
        object remoteToken = new object(), sameValueDifferentToken = new object();
        state.Begin(remoteToken);
        Check(state.ObserveStarting(true, 41), "warning HostHtml is associated");
        Check(!state.IsReady, "starting is not consent readiness");
        Check(!state.ObserveCompleted(true, 40, true), "different HostHtml ID is not associated");
        Check(!state.TryConsume(remoteToken), "cannot proceed before matching completion");
        Check(state.ObserveCompleted(true, 41, true), "matching HostHtml Completed enables consent");
        Check(!state.TryConsume(sameValueDifferentToken), "different token object rejected despite HostHtml ID");
        Check(state.TryConsume(remoteToken), "same remote token can be consumed");
        Check(!state.TryConsume(remoteToken), "consent token is one-shot");
        state.Begin(remoteToken);
        Check(!state.ObserveStarting(false, 42), "generic navigation invalidates pending warning");
        Check(!state.ObserveStarting(true, 43), "generic navigation leaves no eligible warning");
        state.Begin(remoteToken);
        Check(state.ObserveStarting(true, 44), "new associated warning starts");
        Check(!state.ObserveCompleted(true, 44, false), "failed warning terminal is not consent readiness");
        Check(!state.IsReady, "failed warning remains ineligible");
        Console.WriteLine("checks=" + checks);
        Console.WriteLine("failures=" + failures);
        return failures == 0 ? 0 : 1;
    }
}
'@
    Set-Content -LiteralPath "$work\Driver.cs" -Value $driver -Encoding utf8
    & $csc /nologo /define:POLICY_RULES_ONLY /t:exe "/out:$work\BrowserPolicyCheck.exe" $source "$work\Driver.cs"
    if ($LASTEXITCODE -ne 0) { throw "csc failed: $LASTEXITCODE" }
    $output = (& "$work\BrowserPolicyCheck.exe" | Out-String)
    Write-Output $output
    if ($LASTEXITCODE -ne 0) { throw 'Browser-policy verification failed.' }
    if ($output -notmatch 'checks=23' -or $output -notmatch 'failures=0') { throw 'Browser-policy assertion count mismatch.' }
    'Browser-policy verification PASS: 23/23'
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
