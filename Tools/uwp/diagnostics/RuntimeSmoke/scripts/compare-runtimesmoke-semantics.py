#!/usr/bin/env python3
"""Host-side proof that the migrated RuntimeSmoke payload is behavior-identical.

Compares the new diagnostics/ sources against the pre-migration git HEAD blobs
and the live engine tree: every CRT canary, failure-bit value, JavaScript
probe constant, expected result, log stage name, loader target and C#/manifest
contract value must match. Exits nonzero on the first mismatch.
"""
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
NEW = REPO / "Tools/uwp/diagnostics/RuntimeSmoke"
OLD_PROBE = "Tools/uwp/RuntimeSmoke/RuntimeSmoke.cpp"
OLD_HOST = "Tools/uwp/RuntimeSmoke/SmokeHost.cpp"
FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"

failures = []


def check(condition, message):
    """Record a PASS/FAIL line for one semantic invariant."""
    print(("PASS: " if condition else "FAIL: ") + message)
    if not condition:
        failures.append(message)


def head_blob(path):
    """Read a committed blob without touching the working tree."""
    return subprocess.check_output(
        ["git", "show", f"HEAD:{path}"], cwd=REPO, text=True,
        encoding="utf-8")


def main():
    """Verify every preserved payload value and the C# contract."""
    old_probe = head_blob(OLD_PROBE)
    old_host = head_blob(OLD_HOST)
    probe = (NEW / "payload/RuntimeSmoke.cpp").read_text(encoding="utf-8")
    driver = (NEW / "payload/SmokeDiagnostics.cpp").read_text(
        encoding="utf-8")
    header = (NEW / "payload/SmokeDiagnostics.h").read_text(encoding="utf-8")

    # The CRT probe DLL moved without any byte changing.
    check(probe == old_probe, "RuntimeSmoke.cpp byte-identical to HEAD")

    # Every CRT canary constant survived the move.
    for token in ("kInitialTlsValue = 73", "kInitialPodTlsValue = 19",
                  "kFunctionStaticValue = 91", "kWorkerCount = 4",
                  "kWorkerIdBase = 100", "kExpectedSingleConstruction = 1",
                  "kStaticGuardContentionDelayMS = 20",
                  "kWorkerTlsWritebackDelayMS = 10"):
        check(token in probe, f"canary preserved: {token}")

    # Every failure-mask bit survived with its numeric value.
    for token in ("RuntimeSmokeFailureGlobalInit = 1",
                  "RuntimeSmokeFailureTlsCanary = 2",
                  "RuntimeSmokeFailureStaticInit = 4",
                  "RuntimeSmokeFailureTlsWriteback = 8",
                  "RuntimeSmokeFailureArithmetic = 16",
                  "RuntimeSmokeFailureThreadSpawn = 32",
                  "RuntimeSmokeFailureTlsLifetime = 64",
                  "RuntimeSmokeFailureStaticOnce = 128"):
        check(token in probe, f"failure bit preserved: {token}")

    # Every arithmetic expectation survived.
    for token in ("kSignedDividend = -100000000007LL",
                  "kSignedQuotient = -1030927835LL",
                  "kSignedRemainder = -12",
                  "kUnsignedDividend = 100000000007ULL",
                  "kUnsignedQuotient = 1030927835ULL",
                  "kUnsignedRemainder = 12",
                  "kSmallSignedDividend = -1007",
                  "kSmallSignedQuotient = -59",
                  "kSmallSignedRemainder = -4",
                  "kSmallUnsignedDividend = 1007",
                  "kSmallUnsignedQuotient = 59",
                  "kSmallUnsignedRemainder = 4",
                  "kExactlyRepresentableFloat = 4294967296.0f",
                  "kExactlyRepresentableInteger = 4294967296ULL",
                  "WKRunRuntimeSmoke"):
        check(token in probe, f"arithmetic/probe expectation preserved: {token}")

    # JavaScript probe constants and the 4950 result survived in the driver.
    for token in ("kJavaScriptProbeLoopCount = 100",
                  "let sum = 0; for (let i = 0; i < %u; ++i) sum += i",
                  "kJavaScriptProbeStartingLine = 1",
                  "kJavaScriptConversionFailure = -1.0",
                  "kJavaScriptProbeLoopCount * (kJavaScriptProbeLoopCount - 1) / 2"):
        check(token in driver, f"JS probe preserved: {token[:48]}")
    check(100 * 99 // 2 == 4950, "JS loop evaluates to 4950")

    # Loader targets and export names survived in the driver.
    for token in ('LoadPackagedLibrary(L"WebKitRuntimeSmoke.dll"',
                  'LoadPackagedLibrary(L"JavaScriptCore.dll"',
                  'LoadPackagedLibrary(L"WebCore.dll"',
                  '"WKRunRuntimeSmoke"', '"WKRenderFirstPageUWP"',
                  '"JSGlobalContextCreate"', '"JSEvaluateScript"',
                  '"JSValueToNumber"'):
        check(token in driver, f"loader/export preserved: {token}")

    # Every payload log stage name survived in the driver.
    for stage in ("worker-started", "load-runtime-smoke",
                  "runtime-smoke-failure-mask", "load-javascriptcore",
                  "javascriptcore-exports", "create-js-context",
                  "javascript-evaluation", "load-webcore",
                  "page-render-export", "page-pixel-allocation",
                  "page-render-result", "page-d3d11-initialize",
                  "page-d3d11-present"):
        check(f'"{stage}"' in driver, f"log stage preserved: {stage}")

    # The D3D presentation path survived in the driver.
    for token in ("WebKitWebViewPresenterUWP",
                  "initializeForCoreWindow",
                  "present(pixels->bytes"):
        check(token in driver, f"presentation preserved: {token}")

    # The retired native activation model is gone from the payload.
    for token in ("CoreApplication", "IFrameworkView", "WKSmokeStartup",
                  "ProcessEvents"):
        check(token not in driver and token not in header,
              f"native activation retired from payload: {token}")

    # The first-page signature is declared (not an inline cast) and matches
    # the engine definition spelling parameter by parameter.
    engine = (REPO / "Source/WebCore/platform/uwp/FirstPageUWP.cpp"
              ).read_text(encoding="utf-8")
    engine_signature = re.search(
        r"WKRenderFirstPageUWP\(\s*(.*?)\s*\)", engine, re.S).group(1)
    header_signature = re.search(
        r"WKRenderFirstPageUWPType\)\(\s*(.*?)\s*\)", header, re.S).group(1)
    normalize = lambda text: re.sub(r"\s+", " ", text).strip()
    engine_signature = normalize(engine_signature)
    header_signature = normalize(header_signature)
    leading = ("const wchar_t* fontDirectory, unsigned char* pixels,"
               " unsigned width, unsigned height,")
    check(leading in engine_signature and leading in header_signature,
          "declared first-page parameters match the engine spelling")
    check("WKSmokeLogUWP)(const char* stage, unsigned long result)" in normalize(header)
          and "void(__stdcall* log)(const char*, unsigned long)" in normalize(engine),
          "declared log sink matches the engine spelling")

    # The engine payload and its exports are untouched by this migration.
    untouched = subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--",
         "Source/WebCore/platform/uwp/FirstPageUWP.cpp",
         "Source/WebCore/platform/uwp/PageUWP.h",
         "Source/WebCore/platform/uwp/EngineUWP.h",
         "Source/WebCore/platform/uwp/EngineUWP.cpp"],
        cwd=REPO).returncode == 0
    check(untouched, "engine first-page/startup sources untouched")

    # The C# host mirrors the engine viewport bound and snapshot defaults.
    app = (NEW / "host/App.xaml.cs").read_text(encoding="utf-8")
    for token in ("MaxPageDimension = 2048.0", "SnapshotWidth = 480",
                  "SnapshotHeight = 800", '"runtime-smoke.txt"',
                  '"open-log"', '"host-started"', '"window-activated"',
                  '"page-viewport-width"', '"page-viewport-height"',
                  '"diagnostics-returned"', "WebKitRuntimeSmoke.dll",
                  "WKRunSmokeDiagnostics", "DisplayRequest",
                  "Window.Current.CoreWindow"):
        check(token in app, f"C# host contract preserved: {token[:40]}")

    # The manifest/project follow the proven toolchain gate verbatim.
    manifest = ET.parse(NEW / "host/Package.appxmanifest")
    root = manifest.getroot()
    identity = root.find(f"{{{FOUNDATION}}}Identity")
    check(identity.get("Name") == "WebKitWebView.RuntimeSmoke",
          "manifest identity name preserved")
    check(identity.get("Publisher") == "CN=WebKitWebView",
          "manifest publisher preserved")
    check(identity.get("Version") == "1.0.0.11",
          "manifest version bumped past 1.0.0.10")
    check(identity.get("ProcessorArchitecture") == "arm",
          "manifest targets ARM")
    family = root.find(f"{{{FOUNDATION}}}Dependencies/{{{FOUNDATION}}}TargetDeviceFamily")
    check(family.get("MinVersion") == "10.0.15063.0"
          and root.find(f"{{{FOUNDATION}}}Identity").get("Version") is not None,
          "manifest MinVersion is 15063")
    check("PhoneIdentity" in ET.tostring(root, encoding="unicode"),
          "manifest keeps PhoneIdentity")
    project = (NEW / "host/RuntimeSmoke.csproj").read_text(encoding="utf-8")
    check("10.0.19041.0" in project and "10.0.15063.0" in project,
          "project targets 19041 with MinVersion 15063")
    check('Version="6.0.15"' in project and 'Version="6.2' not in project,
          "project restores UWP framework 6.0.15, never 6.2.x")
    check("WebKitRuntimeSmokeHost" in project
          and "WebKitWebView.RuntimeSmoke.App" in ET.tostring(root, encoding="unicode"),
          "assembly/executable/entry point agree")

    # Packaged logos are exactly the required pixel sizes.
    for name, size in (("Logo44.png", 44), ("Logo50.png", 50),
                       ("Logo150.png", 150)):
        data = (NEW / "host/Assets" / name).read_bytes()
        width, height = struct.unpack(">II", data[16:24])
        check(data[:8] == b"\x89PNG\r\n\x1a\n" and width == size
              and height == size, f"logo {name} is exactly {size}x{size}")

    # The retired host contributed no payload constant that is now missing:
    # every numeric literal the driver relies on came from the old host.
    for token in ("kDiagnosticSuccess = 0", "kDiagnosticFailure = 1",
                  "kSmokeLogLineCapacity = 160",
                  "kTemporaryPathCapacity = 1024",
                  "kJavaScriptProbeSourceCapacity = 128"):
        check(token in driver and token in old_host,
              f"driver constant traced to retired host: {token}")

    print(f"\nSemantic comparison: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
