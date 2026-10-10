#!/usr/bin/env python3
"""Report documentation and spacing candidates in authored UWP C/C++ and IDL."""
import argparse
import re
from pathlib import Path


# Repository-relative root used to distinguish authored and generated headers.
ROOT = Path(__file__).resolve().parents[2]
# Authored UWP areas included by the default report.
SOURCE_DIRECTORIES = (
    ROOT / "Source/WebCore/platform/uwp",
    ROOT / "Source/WebKit/UIProcess/API/uwp",
    ROOT / "Tools/uwp",
)
# Historical MIDL output basenames, excluded only in their generated source homes.
GENERATED_FILES = {"WebKitWebView.h", "WebKitWebView_p.h", "WebKitWebView_i.c"}
# The authored API-root WebKitWebView.h must never be mistaken for MIDL output.
GENERATED_DIRECTORIES = {ROOT / "Tools/uwp/CXComponent", ROOT / "Tools/uwp/WinRTComponent"}


def documented(lines, index):
    """Look above declaration attributes for its immediately preceding comment."""
    cursor = index - 1
    while cursor >= 0 and lines[cursor].strip().startswith(("[", "MIDL_INTERFACE")):
        cursor -= 1
    return cursor >= 0 and lines[cursor].lstrip().startswith("//")


def declaration_parameters(text):
    """Distinguish declaration parameters from common local direct initializers."""
    text = re.sub(r"__declspec\([^)]*\)\s*", "", text)
    parameters = text.partition("(")[2].partition(")")[0].strip()
    if not parameters or parameters == "void":
        return True
    return bool(re.match(r"(?:const\s+|unsigned\s+|signed\s+)?(?:[A-Z]\w*|std::\w+|"
                         r"bool|char|wchar_t|int|long|short|double|float|void|uint\d+_t|int\d+_t)\b", parameters))


def inspect(path):
    """Yield human-review candidates; this lexical audit is not a C++ parser."""
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    classes = set(re.findall(r"(?:class|struct)\s+(\w+)", "\n".join(lines)))
    for index, line in enumerate(lines):
        text = line.strip()
        if text.startswith("//") and index:
            previous = lines[index - 1]
            previous_text = previous.strip()
            comment_indent = len(line) - len(line.lstrip())
            previous_indent = len(previous) - len(previous.lstrip())
            if previous_text in ("}", "};") and comment_indent == previous_indent:
                yield index + 1, "comment after completed declaration needs a blank line"
        type_definition = re.match(r"(?:(?:public|private|ref)\s+)*(?:class|struct|enum(?:\s+class)?|interface\s+class|runtimeclass)\s+\w+", text)
        method = re.match(r"(?:template\s*<[^>]+>\s*)?(?:virtual\s+|static\s+|inline\s+|explicit\s+|extern\s+\"C\"\s+|__declspec\([^)]*\)\s+)*"
                          r"(?:[A-Za-z_]\w*(?:::\w+)*(?:<[^;=]+>)?)(?:\s*[*&^])?\s+"
                          r"(?:__stdcall\s+|STDMETHODCALLTYPE\s+)?[\w:~]+\s*\(", text)
        if text.startswith(("return ", "throw ", "case ", "using ", "else ")) or not declaration_parameters(text):
            method = None
        constructor = re.match(r"(?:explicit\s+)?([\w:~]+)\s*\(", text)
        constructor = constructor and (constructor[1].split("::")[-1].lstrip("~") in classes or
                                       ("::" in constructor[1] and constructor[1].split("::")[-1].lstrip("~") == constructor[1].split("::")[-2]))
        property_definition = re.match(r"(?:virtual\s+|static\s+)*property\s+", text)
        event_definition = re.match(r"(?:virtual\s+)*event\s+", text)
        constant = re.match(r"(?:static\s+)?(?:constexpr|const)\s+[^()=;{]+\s+\w+(?:\[\])?\s*(?:=|\{|;)", text)
        if (type_definition and ("{" in text or not text.endswith(";"))) or method or constructor or property_definition or event_definition or constant:
            if not documented(lines, index):
                yield index + 1, "declaration needs documentation: " + text[:100]
        if re.search(r"\bcase\s+\d+\s*:", text):
            yield index + 1, "numeric switch label: " + text


def main():
    """Report candidates, or fail a targeted --check gate, without editing files."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, help="Authored files/directories to inspect; defaults to all UWP areas")
    parser.add_argument("--check", action="store_true", help="Exit nonzero if any documentation/spacing candidate remains")
    args = parser.parse_args()
    paths = set()
    for selected in args.paths or SOURCE_DIRECTORIES:
        selected = selected.resolve()
        paths.update(selected.rglob("*") if selected.is_dir() else (selected,))
    candidates = 0
    for path in sorted(paths):
        if path.suffix not in (".h", ".cpp", ".c", ".idl") or (path.name in GENERATED_FILES and path.parent in GENERATED_DIRECTORIES):
            continue
        for line, message in inspect(path):
            candidates += 1
            print(f"{path.relative_to(ROOT)}:{line}: {message}")
    if args.check:
        print(f"Documentation audit: {candidates} candidates")
        return int(bool(candidates))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
