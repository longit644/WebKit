"""Verify 4.1 source preservation and inspect a built ARM32 engine, without loading it."""

import argparse
from pathlib import Path
import re
import subprocess


# This committed baseline precedes the extraction and remains a rollback reference.
BASELINE = "5f885ad7cc"
REPO = Path(__file__).resolve().parents[3]
W10M = REPO / "Source/WebCore/platform/w10m"

# Ignore braces inside C++ strings/comments when identifying a moved definition.
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]', re.S)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def definition(text, marker):
    """Extract a balanced definition while excluding string/comment braces."""
    start = text.index(marker)
    depth = 0
    for token in TOKEN.finditer(text, start):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return text[start:token.end()]
    raise AssertionError(f"Unclosed definition: {marker}")


def normalized(text):
    """Compare moved code independently of comments and formatting."""
    # Preserve string contents (URLs contain //) while removing actual comments.
    tokens = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)
    text = tokens.sub(lambda match: match.group() if match.group().startswith('"') else "", text)
    return re.sub(r"\s+", "", text)


def check_source():
    """Prove the startup body/strategies/results and smoke renderer were preserved."""
    old = subprocess.check_output(
        ["git", "show", f"{BASELINE}:Source/WebCore/platform/uwp/FirstPageUWP.cpp"],
        cwd=REPO, text=True, encoding="utf-8")
    engine = (W10M / "EngineW10M.cpp").read_text(encoding="utf-8")
    header = (W10M / "EngineW10M.h").read_text(encoding="utf-8")
    first = (W10M / "FirstPageW10M.cpp").read_text(encoding="utf-8")
    page = (W10M / "PageW10M.cpp").read_text(encoding="utf-8")

    old_body = definition(old, "unsigned __stdcall WKInitializeEngineUWP")
    new_body = definition(engine, "unsigned engineEnsureInitializedUWP")
    old_body = old_body[old_body.index("{"):].replace("using namespace WebCore;", "")
    new_body = new_body[new_body.index("{"):]
    assert normalized(old_body) == normalized(new_body), "Startup semantics changed"
    for marker in ("enum FirstPageResultUWP", "class FirstPageMediaStrategy", "class FirstPagePlatformStrategies"):
        destination = header if marker.startswith("enum") else engine
        assert normalized(definition(old, marker)) == normalized(definition(destination, marker)), marker
    assert normalized(definition(old, "unsigned __stdcall WKRenderFirstPageUWP")) == normalized(
        definition(first, "unsigned __stdcall WKRenderFirstPageUWP")), "Smoke rendering changed"
    assert "engineEnsureInitializedUWP(fontDirectory, log)" in definition(first, "unsigned __stdcall WKInitializeEngineUWP")
    assert "engineServiceUWP();" in definition(page, "unsigned __stdcall WKTickPageUWP")
    assert "WTF::RunLoop::cycle()" not in COMMENT.sub("", page), "A second pump implementation remains"
    assert engine.count("WTF::RunLoop::cycle()") == 1
    print("PASS: startup/strategies/results preserved; smoke renderer preserved; shared service implementation")


def check_image(build_dir, readobj):
    """Inspect actual PE architecture and exports; never execute an ARM image here."""
    dll = build_dir / "bin/WebCore.dll"
    headers = subprocess.check_output([str(readobj), "--file-headers", str(dll)], text=True)
    assert "IMAGE_FILE_MACHINE_ARMNT" in headers, "Not ARM32"
    assert "IMAGE_DLL_CHARACTERISTICS_APPCONTAINER" in headers, "Not AppContainer"
    exports = subprocess.check_output([str(readobj), "--coff-exports", str(dll)], text=True)
    old_dll = REPO / "build-arm-uwp-skia/bin/WebCore.dll"
    old_exports = subprocess.check_output([str(readobj), "--coff-exports", str(old_dll)], text=True)
    legacy = set(re.findall(r"^\s*Name: (WK\w+UWP)\s*$", old_exports, re.M))
    names = set(re.findall(r"^\s*Name: (.+)$", exports, re.M))
    assert len(legacy) == 24 and legacy <= names, "Legacy export closure changed"
    assert "WKUWPGetEngineAPI" in names, "Discovery export missing"
    print("PASS: built WebCore.dll ARMNT/AppContainer; 24 legacy exports plus discovery")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--readobj", type=Path, required=True)
    args = parser.parse_args()
    check_source()
    check_image(args.build_dir.resolve(), args.readobj.resolve())
