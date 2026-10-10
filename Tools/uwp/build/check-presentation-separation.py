"""Prove presentation/service/input code never infers network completion."""

import argparse
from pathlib import Path
import re


# Repository root used to resolve the owned framework boundary.
ROOT = Path(__file__).resolve().parents[3]


# Framework areas that must stay free of completion inference and engine coupling.
OWNED_DIRECTORIES = (
    "Source/WebKit/UIProcess/API/uwp/rendering",
    "Source/WebKit/UIProcess/API/uwp/input",
    "Source/WebKit/UIProcess/API/uwp/native",
    "Source/WebKit/UIProcess/API/uwp/platform",
)


# Only the real control may synthesize terminal payloads from structured events.
TERMINAL_CREATORS = {
    "Source/WebKit/UIProcess/API/uwp/WebKitWebView.cpp",
    "Source/WebKit/UIProcess/API/uwp/WebKitNavigationEventArgs.cpp",
    "Source/WebKit/UIProcess/API/uwp/WebKitNavigationEventArgs.h",
}


def uncomment(source: str) -> str:
    """Remove prose while preserving string literals used by real implementation code."""
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return token.sub(lambda match: "" if match[0].startswith(("//", "/*")) else match[0], source)


def verify_tree(repository: Path) -> None:
    """Fail when owned presentation code infers completion or couples to the engine."""
    owned = [repository / name for name in OWNED_DIRECTORIES]
    assert all(path.is_dir() for path in owned), "Owned presentation boundary is missing"
    boundary = repository / "Source/WebKit/UIProcess/API/uwp"
    assert boundary.is_dir(), "Public control boundary is missing"
    for directory in owned:
        for path in sorted(directory.rglob("*")):
            if path.suffix not in (".h", ".cpp") or not path.is_file():
                continue
            name = path.relative_to(repository).as_posix()
            source = uncomment(path.read_text(encoding="utf-8-sig"))
            assert not re.search(r"(?:find|strstr|strcmp)\s*\([^\n]*(?:navigation-completed|load-finished|page-log)", source), \
                f"Log-derived completion inference in presentation code: {name}"
            assert not re.search(r"uwp-suite=|fixture-suite|192\.168\.1\.|example\.test|about:demo", source), \
                f"Diagnostic fixture content in presentation code: {name}"
            assert not re.search(r"#\s*include\s*[<\"][^>\"]*(?:LoaderStrategy|CurlRequest|curl/curl|CachedResource|"
                                 r"DocumentLoader|LocalFrame|SubresourceLoader|TextureMapper|GraphicsLayer|"
                                 r"PageUWP|wtf/|pal/|WebCore/)[^>\"]*[>\"]", source), \
                f"Engine loader/DOM coupling in presentation code: {name}"
            assert not re.search(r"(?i)\bcurl", source), \
                f"Transport downloader reference in presentation code: {name}"
    for path in sorted(boundary.rglob("*")):
        if path.suffix not in (".h", ".cpp") or not path.is_file():
            continue
        name = path.relative_to(repository).as_posix()
        if "ref new WebKitNavigationCompletedEventArgs" in uncomment(path.read_text(encoding="utf-8-sig")):
            assert name in TERMINAL_CREATORS, f"Terminal payload synthesized outside the control: {name}"
    print("Presentation separation PASS: no completion inference, fixture, engine or downloader coupling")


def main() -> int:
    """Validate the owned presentation boundary and print a fail-closed result."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=ROOT)
    args = parser.parse_args()
    verify_tree(args.repository)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
