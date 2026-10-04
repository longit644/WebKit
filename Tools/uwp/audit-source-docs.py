#!/usr/bin/env python3
"""Report documentation and spacing candidates in authored UWP C/C++ and IDL."""
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRECTORIES = (
    ROOT / "Source/WebCore/platform/uwp",
    ROOT / "Source/WebKit/UIProcess/API/uwp",
    ROOT / "Tools/MiniBrowser/uwp",
    ROOT / "Tools/uwp",
)
GENERATED_FILES = {"WebKitWebView.h", "WebKitWebView_p.h", "WebKitWebView_i.c"}


def documented(lines, index):
    """Look above declaration attributes for its immediately preceding comment."""
    cursor = index - 1
    while cursor >= 0 and lines[cursor].strip().startswith(("[", "MIDL_INTERFACE")):
        cursor -= 1
    return cursor >= 0 and lines[cursor].lstrip().startswith("//")


def inspect(path):
    """Yield human-review candidates; this lexical audit is not a C++ parser."""
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    for index, line in enumerate(lines):
        text = line.strip()
        if text.startswith("//") and index:
            previous = lines[index - 1]
            previous_text = previous.strip()
            comment_indent = len(line) - len(line.lstrip())
            previous_indent = len(previous) - len(previous.lstrip())
            if previous_text in ("}", "};") and comment_indent == previous_indent:
                yield index + 1, "comment after completed declaration needs a blank line"
        type_definition = re.match(r"(?:class|struct|enum|interface|runtimeclass)\s+\w+", text)
        method = re.match(r"(?:virtual\s+|static\s+|inline\s+|explicit\s+|extern\s+\"C\"\s+|__declspec\([^)]*\)\s+)*"
                          r"(?:IFACEMETHODIMP|STDAPI|HRESULT|void|bool|unsigned|int|double|String|ResourceError|"
                          r"RefPtr<[^>]+>|Ref<[^>]+>|Vector<[^>]+>|[A-Za-z_]\w*\*)\s+"
                          r"(?:__stdcall\s+|STDMETHODCALLTYPE\s+)?[\w:~]+\s*\(", text)
        if (type_definition and not text.endswith(";")) or method:
            if text.startswith("String "):
                continue
            if not documented(lines, index):
                yield index + 1, "declaration needs documentation: " + text[:100]
        if re.search(r"\bcase\s+\d+\s*:", text):
            yield index + 1, "numeric switch label: " + text


def main():
    """Print the full candidate list for review without modifying any source files."""
    for directory in SOURCE_DIRECTORIES:
        for path in sorted(directory.rglob("*")):
            if path.suffix not in (".h", ".cpp", ".c", ".idl") or path.name in GENERATED_FILES:
                continue
            for line, message in inspect(path):
                print(f"{path.relative_to(ROOT)}:{line}: {message}")


if __name__ == "__main__":
    main()
