"""Fail when UWP engine headers depend on C++/CX or WinRT language projections."""

import argparse
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
ENGINE_DIR = REPO / "Source/WebCore/platform/uwp"

# C++/CX or WinRT projection dependencies: WRL and WinRT headers, the
# Windows:: namespace, and ref-class declarations. Plain Win32 headers such as
# windows.h are platform API, not projections, and stay allowed. Matches run
# against code with comments and string literals removed, so prose mentioning
# WinRT (for example ClipboardUWP.h ownership notes) never fails the gate.
PATTERNS = (
    r'#include\s+<wrl\.h>',
    r'#include\s+<wrl/',
    r'#include\s+<windows\.ui\.',
    r'#include\s+<windows\.foundation\.',
    r'(?<![\w:])Windows::',
    r'(?<![\w:])ref\s+class\s+\w+',
    r'(?<![\w:])ref\s+struct\s+\w+',
    r'#using\s+',
    r'__declspec\s*\(\s*uuid\s*\(',
)
STRINGS_COMMENTS = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)


def violations(path):
    """Yield (line, text) for projection dependencies outside prose."""
    text = path.read_text(encoding="utf-8-sig")
    stripped = STRINGS_COMMENTS.sub(
        lambda match: '""' if match.group().startswith('"') else "''"
        if match.group().startswith("'") else "", text)
    findings = []
    for number, line in enumerate(stripped.splitlines(), start=1):
        for pattern in PATTERNS:
            if re.search(pattern, line):
                findings.append((number, line.strip()))
                break
    return findings


def main():
    """Scan every engine header; fail on any hit.

    Only headers are gated: they are the contract surface other code builds
    against. Implementation files may still use WRL helpers and WinRT headers
    internally (ClipboardUWP, PageUWP and TextureMapperCompositorUWP do today);
    section 7.1 migrates those behind neutral service callbacks.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    args = parser.parse_args()
    failures = []
    for path in sorted(ENGINE_DIR.glob("*.h")):
        if not path.is_file():
            continue
        for line, text in violations(path):
            failures.append(f"{path.relative_to(REPO)}:{line}: {text}")
    if failures:
        print("\n".join(failures))
        raise SystemExit(
            f"C++/CX or WinRT projection dependency in {len(failures)} place(s)")
    print(f"PASS: no C++/CX or WinRT projection dependency in {ENGINE_DIR.name} headers")


if __name__ == "__main__":
    main()
