"""Check the actual component inputs and structured event source-of-truth contract."""

import argparse
import json
from pathlib import Path
import re


# Repository-relative authored boundary; tooling/prototype/generated output is not its authority.
ROOT = Path(__file__).resolve().parents[3]


def uncomment(source: str) -> str:
    """Remove prose while preserving string literals used by real implementation code."""
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return token.sub(lambda match: "" if match[0].startswith(("//", "/*")) else match[0], source)


def verify_sources(repository: Path, sources: list[str]) -> None:
    """Audit compiled authored inputs so a prototype/log-derived projection cannot pass."""
    assert sources, "No actual compiler source manifest supplied"
    expected = {
        "Source/WebKit/UIProcess/API/w10m/WebKitWebView.cpp",
        "Source/WebKit/UIProcess/API/w10m/WebKitNavigationEventArgs.cpp",
        "Source/WebKit/UIProcess/API/w10m/WebKitCertificateFailureToken.cpp",
        "Source/WebKit/UIProcess/API/w10m/platform/PageCallbacksW10M.cpp",
        "Source/WebKit/UIProcess/API/w10m/platform/InitializationActionW10M.cpp",
        "Source/WebKit/UIProcess/API/w10m/native/PageControllerW10M.cpp",
        "Source/WebKit/UIProcess/API/w10m/native/EngineModuleW10M.cpp",
    }
    assert expected <= set(sources), "Component omitted an authored public/structured boundary source"
    for name in sources:
        assert name not in {
            "Tools/w10m/CXComponent/WebKitWebViewComponent.cpp",
            "Source/WebKit/UIProcess/API/w10m/WebKitWebViewW10M.cpp",
        }, f"Historical prototype/wrapper compiled into new component: {name}"
        if not name.startswith("Source/WebKit/UIProcess/API/w10m/"):
            continue
        source = uncomment((repository / name).read_text(encoding="utf-8-sig"))
        assert not re.search(r"\b(?:WKPageLogUWP|WKPageLogBooleanUWP|WKNavigationPolicyCallback)\b", source), f"Legacy log/policy projection in authored boundary: {name}"
        assert not re.search(r"#\s*include\s*[<\"][^>\"]*(?:WebKitWebView_i|WebKitWebView_p|WebKitWinRTBase)", source), f"Prototype generated/header authority used: {name}"
        assert not re.search(r"(?:uwp-suite=|fixture-suite|example\.test)", source), f"Diagnostic content in framework source: {name}"
    view = uncomment((repository / "Source/WebKit/UIProcess/API/w10m/WebKitWebView.cpp").read_text(encoding="utf-8-sig"))
    bridge = uncomment((repository / "Source/WebKit/UIProcess/API/w10m/platform/PageCallbacksW10M.cpp").read_text(encoding="utf-8-sig"))
    assert re.search(r"HandleStarting\(const WKW10MPageEvent&", view), "Policy does not consume frozen structured data"
    assert re.search(r"HandleNotification\(const WKW10MPageEvent&", view), "Notifications do not consume frozen structured data"
    assert "target->HandleStarting(*event, cancel)" in bridge and "target->HandleNotification(*event)" in bridge, "Native table is not routed to the real structured control"
    assert "event.errorCode" in view and "event.hasHttpStatus" in view and "event.certificateToken" in view, "Public terminal payload is not copied from native fields"
    assert "ref new WebKitNavigationCompletedEventArgs" in view and "ref new WebKitNavigationStartingEventArgs" in view, "Public typed objects are not created at structured entry points"
    assert not re.search(r"(?:find|strstr|strcmp)\s*\([^\n]*(?:navigation-completed|load-finished|page-log)", view), "Public delivery derived from diagnostic strings"


def main() -> int:
    """Validate one built component's real source list and print a fail-closed result."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", required=True, type=Path)
    parser.add_argument("--repository", type=Path, default=ROOT)
    args = parser.parse_args()
    manifest = json.loads(args.identity.read_text(encoding="utf-8-sig"))
    assert manifest.get("ContractRevision") == 2, "Unexpected public contract revision"
    verify_sources(args.repository, manifest.get("Sources", []))
    print("Public contract source PASS: actual authored inputs, frozen callbacks, copied fields, no prototype/log-derived events")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
