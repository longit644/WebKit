#requires -Version 7.0
<#
.SYNOPSIS
Host-runnable unit check for the visible-bounds margin math (M10 task 8.1).
Compiles the shipped Tools/MiniBrowser/csharp/VisibleBoundsHelper.cs with the
desktop compiler and executes 31 vectors (124 margin assertions).
#>
[CmdletBinding()]
param(
    [string]$MsBuildPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$helper = Join-Path $repo 'Tools\MiniBrowser\csharp\VisibleBoundsHelper.cs'
$csc = Join-Path $MsBuildPath 'Roslyn\csc.exe'
foreach ($required in @($helper, $csc)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required math-check input missing: $required" }
}
$work = Join-Path ([IO.Path]::GetTempPath()) ('opencode\visible-bounds-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    $driver = @'
using System;
/// <summary>Runs the visible-bounds margin test vectors.</summary>
sealed class VisibleBoundsDriver
{
    /// <summary>The number of failed assertions.</summary>
    static int failures = 0;
    /// <summary>The number of assertions performed.</summary>
    static int checks = 0;
    /// <summary>Checks one computed margin against its expected value.</summary>
    /// <param name="actual">The computed margin.</param>
    /// <param name="expected">The expected margin.</param>
    /// <param name="name">The assertion label.</param>
    static void Check(double actual, double expected, string name)
    {
        checks++;
        if (actual != expected) { failures++; Console.WriteLine("FAIL: " + name + " actual=" + actual + " expected=" + expected); }
    }
    /// <summary>Runs one window and visible-bounds vector.</summary>
    /// <param name="x">Window left.</param>
    /// <param name="y">Window top.</param>
    /// <param name="w">Window width.</param>
    /// <param name="h">Window height.</param>
    /// <param name="vx">Visible-bounds left.</param>
    /// <param name="vy">Visible-bounds top.</param>
    /// <param name="vw">Visible-bounds width.</param>
    /// <param name="vh">Visible-bounds height.</param>
    /// <param name="el">Expected left margin.</param>
    /// <param name="et">Expected top margin.</param>
    /// <param name="er">Expected right margin.</param>
    /// <param name="eb">Expected bottom margin.</param>
    /// <param name="name">The vector label.</param>
    static void Vector(double x, double y, double w, double h, double vx, double vy, double vw, double vh,
        double el, double et, double er, double eb, string name)
    {
        var m = WebKitMiniBrowser.VisibleBoundsHelper.ComputeMargins(x, y, w, h, vx, vy, vw, vh);
        Check(m.Left, el, name + ".left"); Check(m.Top, et, name + ".top");
        Check(m.Right, er, name + ".right"); Check(m.Bottom, eb, name + ".bottom");
    }
    /// <summary>Runs one window, visible-bounds and input-pane vector.</summary>
    /// <param name="x">Window left.</param>
    /// <param name="y">Window top.</param>
    /// <param name="w">Window width.</param>
    /// <param name="h">Window height.</param>
    /// <param name="vx">Visible-bounds left.</param>
    /// <param name="vy">Visible-bounds top.</param>
    /// <param name="vw">Visible-bounds width.</param>
    /// <param name="vh">Visible-bounds height.</param>
    /// <param name="occludedX">Input-pane left relative to the window.</param>
    /// <param name="occludedY">Input-pane top relative to the window.</param>
    /// <param name="occludedWidth">Input-pane width.</param>
    /// <param name="occludedHeight">Input-pane height.</param>
    /// <param name="el">Expected left margin.</param>
    /// <param name="et">Expected top margin.</param>
    /// <param name="er">Expected right margin.</param>
    /// <param name="eb">Expected bottom margin.</param>
    /// <param name="name">The vector label.</param>
    static void KeyboardVector(double x, double y, double w, double h, double vx, double vy, double vw, double vh,
        double occludedX, double occludedY, double occludedWidth, double occludedHeight,
        double el, double et, double er, double eb, string name)
    {
        var m = WebKitMiniBrowser.VisibleBoundsHelper.ComputeMargins(x, y, w, h, vx, vy, vw, vh,
            occludedX, occludedY, occludedWidth, occludedHeight);
        Check(m.Left, el, name + ".left"); Check(m.Top, et, name + ".top");
        Check(m.Right, er, name + ".right"); Check(m.Bottom, eb, name + ".bottom");
    }
    /// <summary>Runs all test vectors and returns the assertion result.</summary>
    /// <returns>Zero when all assertions pass; otherwise one.</returns>
    static int Main()
    {
        Vector(0, 0, 731, 343, 0, 0, 731, 343, 0, 0, 0, 0, "full-window");
        Vector(0, 0, 731, 343, 0, 0, 635, 343, 0, 0, 96, 0, "landscape-nav-bar");
        Vector(0, 0, 411, 663, 24, 32, 387, 631, 24, 32, 0, 0, "left-top-inset");
        Vector(0, 0, 400, 400, -10, -20, 410, 400, 0, 0, 0, 20, "negative-origin-clamped");
        Vector(0, 0, 0, 400, 0, 0, 0, 400, 0, 0, 0, 0, "degenerate-window");
        Vector(0, 0, 400, 400, 0, 0, 400, 0, 0, 0, 0, 0, "degenerate-visible");
        Vector(100, 100, 500, 500, 120, 130, 440, 420, 20, 30, 40, 50, "all-sides");

        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 300, 0, 24, 0, 300, "portrait-nav-bar-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 252, 0, 24, 0, 300, "portrait-keyboard-ends-above-nav-bar");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 407, 0, 431, 411, 300, 0, 24, 0, 300, "visible-already-at-keyboard-top");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 408, 0, 431, 411, 300, 0, 24, 0, 300, "visible-partly-reflects-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 387, 0, 431, 411, 300, 0, 24, 0, 320, "visible-above-keyboard-top");
        KeyboardVector(0, 0, 731, 411, 0, 24, 635, 387, 0, 211, 731, 200, 0, 24, 96, 200, "landscape-side-bar-keyboard");
        KeyboardVector(0, 0, 731, 411, 0, 24, 635, 387, 635, 211, 96, 200, 0, 24, 96, 0, "keyboard-only-in-side-bar");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 0, 300, 0, 24, 0, 48, "hidden-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 0, 0, 24, 0, 48, "zero-height-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, -411, 300, 0, 24, 0, 48, "negative-width-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 411, 431, 100, 300, 0, 24, 0, 48, "keyboard-outside-horizontally");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 731, 411, 300, 0, 24, 0, 48, "keyboard-outside-vertically");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 200, 411, 300, 0, 24, 0, 48, "floating-keyboard");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 251, 0, 24, 0, 300, "keyboard-at-bottom-tolerance");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 250.5, 0, 24, 0, 48, "keyboard-outside-bottom-tolerance");
        KeyboardVector(100, 200, 411, 731, 100, 224, 411, 659, 0, 431, 411, 300, 0, 24, 0, 300, "nonzero-window-origin-local-keyboard");
        KeyboardVector(0, 0, 411, 400, 0, 24, 411, 376, 0, 431, 411, 300, 0, 24, 0, 0, "window-resized-above-old-keyboard");
        KeyboardVector(0, 0, 411, 431, 0, 24, 411, 407, 0, 431, 411, 300, 0, 24, 0, 0, "window-resized-to-old-keyboard-top");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, -20, 411, 751, 0, 24, 0, 707, "keyboard-bounded-to-usable-height");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, double.NaN, 431, 411, 300, 0, 24, 0, 48, "nonfinite-keyboard-origin");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, double.PositiveInfinity, 0, 24, 0, 48, "nonfinite-keyboard-extent");
        KeyboardVector(0, 0, 400, double.MaxValue, 0, 0, 400, double.MaxValue, 0, double.MaxValue / 2, 400, double.MaxValue, 0, 0, 0, 0, "overflowing-keyboard-bottom");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, 0, 431, 411, 400, 0, 24, 0, 300, "keyboard-extends-below-window");
        KeyboardVector(0, 0, 411, 731, 0, 24, 411, 659, -100, 431, 200, 300, 0, 24, 0, 300, "keyboard-partial-horizontal-overlap");
        Console.WriteLine("checks=" + checks);
        Console.WriteLine("failures=" + failures);
        return failures == 0 ? 0 : 1;
    }
}
'@
    Set-Content -LiteralPath "$work\Driver.cs" -Value $driver -Encoding utf8
    & $csc /nologo /t:exe "/out:$work\VisibleBoundsCheck.exe" $helper "$work\Driver.cs"
    if ($LASTEXITCODE -ne 0) { throw "csc failed: $LASTEXITCODE" }
    $output = (& "$work\VisibleBoundsCheck.exe" | Out-String)
    Write-Output $output
    if ($LASTEXITCODE -ne 0) { throw "Visible-bounds math check failed" }
    if ($output -notmatch 'checks=124' -or $output -notmatch 'failures=0') { throw 'Visible-bounds math check count mismatch' }
    'Visible-bounds math PASS: 124/124'
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
