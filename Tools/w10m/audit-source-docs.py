#!/usr/bin/env python3
"""Report documentation and spacing candidates in authored W10M C/C++ and IDL.

Authored W10M placement (implemented M10 layout; see Tools/w10m/DESIGN.md):

- ``Source/WebKit/Shared/w10m/`` holds the frozen versioned neutral C ABI shared
  by the dual-compiler boundary. Plain C/C++, never C++/CX (``/ZW``); layout is
  asserted by ``Tools/w10m/build/check-wkw10m-abi-layout.ps1``.
- ``Source/WebCore/platform/w10m/`` holds the native engine adapters. Ordinary
  C/C++ with engine exceptions disabled; it must not depend on C++/CX headers
  or compile wholesale with ``/ZW``.
- ``Source/WebKit/UIProcess/API/w10m/`` holds the public C++/CX control together
  with its decomposed services. The ``/ZW`` scope is the root control/options/
  event/token sources plus the private CX ``platform/`` drivers (for example
  ``FrameSchedulerDriverW10M``); ``native/``, ``rendering/``, ``input/`` and the
  neutral ``platform/`` clipboard/runtime services compile without ``/ZW``. The
  exact split is recorded by
  ``Tools/w10m/build/build-arm32-cx-component.ps1`` (``$files`` versus
  ``$nativeFiles``).
- ``Tools/w10m/`` holds platform/build-support tooling: ``build/`` orchestration
  and host contract tests, ``CXComponent/`` component/build support,
  ``CXCompiledProbe/``/``CXWebViewCompiledProbe/``/``CXStateTests/`` device
  probes, ``diagnostics/`` app hosts, ``toolchain/`` native runtime sources and
  ``NavigationTests/`` fixture servers. Authored ``.h``/``.cpp``/``.c``/``.idl``
  here is audited; generator output is excluded as below.

Authored-versus-generated exclusions: MIDLRT outputs (``WebKitWebView.h``,
``WebKitWebView_p.h``, ``WebKitWebView_i.c`` and siblings such as
``XamlProbe_*``) are build outputs and belong in build directories; legacy
copies may still sit in ``Tools/w10m/CXComponent/``, in a ``legacy-generated/``
directory, or at the repository root from recipes that omitted an output path.
XAML/MIDL generated sources (``*.g.*``, for example ``MainPage.g.h``) are
likewise never hand-edited. None of these participate in the audit; the
authored API-root ``Source/WebKit/UIProcess/API/w10m/WebKitWebView.h`` is
always audited.
"""
import argparse
import re
from pathlib import Path


# Repository-relative root used to distinguish authored and generated headers.
ROOT = Path(__file__).resolve().parents[2]
# Authored W10M areas included by the default report.
SOURCE_DIRECTORIES = (
    ROOT / "Source/WebKit/Shared/w10m",
    ROOT / "Source/WebCore/platform/w10m",
    ROOT / "Source/WebKit/UIProcess/API/w10m",
    ROOT / "Tools/w10m",
)
# Historical MIDL output basenames, excluded only in their generated source homes.
GENERATED_FILES = {
    "WebKitWebView.h",
    "WebKitWebView_p.h",
    "WebKitWebView_p.c",
    "WebKitWebView_i.c",
    "XamlProbe.h",
    "XamlProbe_p.h",
    "XamlProbe_p.c",
    "XamlProbe_i.c",
    "dlldata.c",
}
# The authored API-root WebKitWebView.h must never be mistaken for MIDL output.
GENERATED_DIRECTORIES = {ROOT / "Tools/w10m/CXComponent", ROOT}
# Directory names that always hold generator output, whether legacy or current.
GENERATED_DIR_NAMES = {"legacy-generated", "Generated", "GeneratedFiles"}


def is_generated(path):
    """Decide whether a candidate path is generator-owned build output."""
    resolved = path.resolve()
    if ".g." in resolved.name:
        return True
    current = resolved.parent
    while True:
        if current.name in GENERATED_DIR_NAMES:
            return True
        if current == ROOT or current == current.parent:
            break
        current = current.parent
    return resolved.name in GENERATED_FILES and resolved.parent in GENERATED_DIRECTORIES


def documented(lines, index):
    """Look above declaration attributes or a contiguous constant group comment."""
    cursor = index - 1
    while cursor >= 0 and lines[cursor].strip().startswith(("[", "MIDL_INTERFACE", "#if", "#else", "#endif")):
        cursor -= 1
    if cursor >= 0:
        previous = lines[cursor].lstrip()
        if previous.startswith("//"):
            return True
        if previous.endswith("*/"):
            while cursor >= 0:
                if "/*" in lines[cursor]:
                    return True
                cursor -= 1

    indentation = len(lines[index]) - len(lines[index].lstrip())
    constant = re.compile(r"(?:static\s+)?(?:constexpr|const)\s+[^()=;{]+\s+\w+(?:\[\])?\s*(?:=|\{|;)")
    while cursor >= 0 and lines[cursor].strip():
        previous = lines[cursor]
        previous_indentation = len(previous) - len(previous.lstrip())
        if previous_indentation != indentation or not constant.match(previous.strip()):
            break
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


def function_body_lines(lines):
    """Return lines inside function bodies so local values aren't API candidates."""
    scopes = []
    statement = ""
    in_block_comment = False
    result = set()
    controls = {"if", "for", "while", "switch", "catch", "sizeof", "decltype", "return"}

    for index, line in enumerate(lines):
        if any(scope == "function" for scope in scopes):
            result.add(index)

        # Remove comments and ordinary string/character literals before counting
        # braces; braces inside markup, messages or comments are not C++ scopes.
        code = line
        if in_block_comment:
            end = code.find("*/")
            if end < 0:
                continue
            code = code[end + 2:]
            in_block_comment = False
        while code:
            comment = code.find("//")
            block = code.find("/*")
            starts = [position for position in (comment, block) if position >= 0]
            if not starts:
                break
            position = min(starts)
            if position == comment:
                code = code[:position]
                break
            end = code.find("*/", position + 2)
            if end < 0:
                code = code[:position]
                in_block_comment = True
                break
            code = code[:position] + code[end + 2:]
        code = re.sub(r'R"[^\s(]*\(.*?\)[^\s"]*"', '""', code)
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', code)

        for character in code:
            if character == "{":
                context = statement.strip()
                signature = context.rsplit("}", 1)[-1].rsplit(";", 1)[-1].strip()
                function = False
                if "=" not in signature and not re.match(r"(?:else\s+)?(?:if|for|while|switch|catch|namespace|class|struct|enum)\b", signature):
                    match = re.search(r"([~\w:]+)\s*\([^;{}]*\)\s*(?:(?:const|noexcept|override|final|volatile)\s*)*$", signature)
                    function = bool(match and match.group(1).split("::")[-1] not in controls)
                scopes.append("function" if function else "block")
                statement = ""
            elif character == "}":
                if scopes:
                    scopes.pop()
                statement = ""
            elif character == ";":
                statement = ""
            else:
                statement += character

        if not code.strip() and not statement:
            continue
        if code.strip().endswith(("{", "}", ";")):
            continue
        statement += " "

    return result


def inspect(path):
    """Yield human-review candidates; this lexical audit is not a C++ parser."""
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    classes = set(re.findall(r"(?:class|struct)\s+(\w+)", "\n".join(lines)))
    function_lines = function_body_lines(lines)
    for index, line in enumerate(lines):
        text = line.strip()
        if text.startswith("//") and index:
            previous = lines[index - 1]
            previous_text = previous.strip()
            comment_indent = len(line) - len(line.lstrip())
            previous_indent = len(previous) - len(previous.lstrip())
            if previous_text in ("}", "};") and comment_indent == previous_indent:
                yield index + 1, "comment after completed declaration needs a blank line"
        if index in function_lines:
            if re.search(r"\bcase\s+\d+\s*:", text):
                yield index + 1, "numeric switch label: " + text
            continue
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
        if re.match(r"(?:virtual\s+)?(?:[\w^:<>]+\s+)?(?:get|set)\s*\(", text):
            method = None
        if (type_definition and ("{" in text or not text.endswith(";"))) or method or constructor or property_definition or event_definition or constant:
            if not documented(lines, index):
                yield index + 1, "declaration needs documentation: " + text[:100]
        if re.search(r"\bcase\s+\d+\s*:", text):
            yield index + 1, "numeric switch label: " + text


def main():
    """Report candidates, or fail a targeted --check gate, without editing files."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path, help="Authored files/directories to inspect; defaults to all W10M areas")
    parser.add_argument("--check", action="store_true", help="Exit nonzero if any documentation/spacing candidate remains")
    args = parser.parse_args()
    paths = set()
    for selected in args.paths or SOURCE_DIRECTORIES:
        selected = selected.resolve()
        paths.update(selected.rglob("*") if selected.is_dir() else (selected,))
    candidates = 0
    for path in sorted(paths):
        if path.suffix not in (".h", ".cpp", ".c", ".idl") or is_generated(path):
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
