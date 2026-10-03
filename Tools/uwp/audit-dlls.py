#!/usr/bin/env python3
"""Inventory ARM DLL imports/TLS recursively without loading host DLLs.

This reports dependencies and review candidates, not API certification.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


REVIEW_APIS = {
    "AdjustTokenPrivileges", "LookupPrivilegeValueW", "OpenProcessToken",
    "GetConsoleScreenBufferInfo", "WriteConsoleA", "GetVersionExW",
    "LoadLibraryA", "LoadLibraryW", "GetModuleHandleA", "GetModuleHandleW",
    "CreateFileW", "FindFirstFileW", "CreateHardLinkW", "MoveFileWithProgressW",
}


def inspect(path, reader):
    result = subprocess.run(
        [reader, "--file-headers", "--coff-imports", "--coff-tls-directory", str(path)],
        check=True, capture_output=True, text=True, encoding="utf-8")
    text = result.stdout
    imports = []
    for block in re.findall(r"^Import \{\n(.*?)^\}", text, re.M | re.S):
        name = re.search(r"^  Name: (.+)$", block, re.M).group(1)
        symbols = re.findall(r"^  Symbol: (.*?) \((\d+)\)$", block, re.M)
        imports.append({"dll": name, "symbols": [symbol or "ordinal:" + ordinal for symbol, ordinal in symbols]})
    tls = re.search(r"TLSTableRVA: (0x[0-9A-Fa-f]+)", text)
    return {
        "path": str(path),
        "armnt": "Machine: IMAGE_FILE_MACHINE_ARMNT" in text,
        "appcontainer": "IMAGE_DLL_CHARACTERISTICS_APPCONTAINER" in text,
        "tls_rva": tls.group(1) if tls else None,
        "imports": imports,
        "review": sorted({symbol for item in imports for symbol in item["symbols"] if symbol in REVIEW_APIS}),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    parser.add_argument("--search", action="append", default=[], type=Path)
    parser.add_argument("--reader", default="llvm-readobj")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    directories = list(dict.fromkeys([path.parent for path in args.roots] + args.search))
    available = {}
    for directory in directories:
        for path in directory.glob("*.dll"):
            available.setdefault(path.name.lower(), path.resolve())
    pending = [path.resolve() for path in args.roots]
    seen = set()
    records = []
    external = set()
    while pending:
        path = pending.pop(0)
        if path in seen:
            continue
        seen.add(path)
        record = inspect(path, args.reader)
        records.append(record)
        for dependency in record["imports"]:
            local = available.get(dependency["dll"].lower())
            if local:
                pending.append(local)
            else:
                external.add(dependency["dll"])
    report = {"binaries": records, "external_dependencies": sorted(external),
              "note": "External dependencies require device/framework resolution; this is not a legality verdict."}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for record in records:
        print(f"{Path(record['path']).name}: ARMNT={record['armnt']} AppContainer={record['appcontainer']} TLS={record['tls_rva']}")
        if record["review"]:
            print("  Review: " + ", ".join(record["review"]))
    print("External dependencies: " + ", ".join(sorted(external)))
    if any(not record["armnt"] for record in records):
        raise SystemExit("Non-ARMNT binary in dependency chain")


if __name__ == "__main__":
    main()
