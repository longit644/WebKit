#!/usr/bin/env python3
"""Reject bad M10 staging candidates before package creation (task 10.4).

Reuses the audited recursive-import machinery in Tools/w10m/audit-dlls.py
(``audit.inspect`` per binary) instead of duplicating it, then enforces the
gates stage-app.py leaves open: negative ARM64 images, incompatible ABI
(non-ARMNT or non-AppContainer on ANY staged binary, not just the closure
roots), missing assets (fonts/CA/demo/blocked per package kind), conflicting
component metadata, and non-Skia engine candidates where Skia is required.

Every failure prints a ``REJECT:`` line and exits nonzero with no package
identity emitted, so callers (stage-engine-regression.py, MakeAppx drivers)
cannot pack the candidate. Success prints ``AUDIT PASS`` and exits zero.
"""
import argparse
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from importlib import import_module

audit = import_module("audit-dlls")

FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"

# Wrong-architecture machine markers: llvm-readobj reports ARM64 images as
# COFF-ARM64 / IMAGE_FILE_MACHINE_ARM64 / AArch64. None of these substrings
# appear in ARMNT ("COFF-ARM") or x64 ("COFF-x86-64") reports, so a match is
# an unambiguous negative-architecture signal, never a substring accident.
ARM64_MARKERS = re.compile(r"ARM64|AArch64")

# Engine payload every kind stages from its engine input.
ENGINE_LIBS = ("WebCore.dll", "JavaScriptCore.dll")

# Skia/ANGLE GL stack: a Cairo engine candidate has no such payload, so its
# absence (or a non-conforming image) rejects a --require-skia stage.
SKIA_LIBS = ("libEGL.dll", "libGLESv2.dll")

# Canonical device fonts staged by the shared recipe (stage-app.py).
FONT_FILES = ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "LICENSE.dejavu")

# Fixture trust bundle the engine-regression stage carries beside the host.
ENGINE_CA_FILES = ("fixture-ca.pem", "fixture-unrelated-ca.pem")

# MiniBrowser navigation-error template owned by its host project.
BLOCKED_TEMPLATE = "blocked.html"

# Staged images exempt from the AppContainer characteristic requirement.
# icudt78.dll is ICU's data-only payload (no executable code; measured
# ARMNT without APPCONTAINER in device-verified stages while its code
# siblings icuuc78/icuinn carry the flag). Exemptions are recorded in the
# audit report, never silent; ARMNT and the ARM64-negative gate still apply.
DEFAULT_APPCONTAINER_EXEMPT = ("icudt78.dll",)

# Exports the RuntimeSmoke payload must carry (mirrors the staging recipe's
# own post-stage assertion in stage-runtimesmoke.py).
RUNTIME_EXPORTS = ("WKRunRuntimeSmoke", "WKRunSmokeDiagnostics")


def read_headers(path, reader):
    """Return llvm-readobj file-headers text without loading the image."""
    result = subprocess.run(
        [reader, "--file-headers", str(path)],
        check=True, capture_output=True, text=True, encoding="utf-8")
    return result.stdout


def check_binary(path, reader, rejects, require_appcontainer=True):
    """Enforce negative-ARM64 + ARMNT/AppContainer ABI on one staged image."""
    headers = read_headers(path, reader)
    if ARM64_MARKERS.search(headers):
        rejects.append(f"REJECT: negative ARM64 image staged: {path.name}")
        return None
    record = audit.inspect(path, reader)
    if not record["armnt"]:
        rejects.append(f"REJECT: incompatible ABI (not ARMNT): {path.name}")
    if require_appcontainer and not record["appcontainer"]:
        rejects.append(f"REJECT: incompatible ABI (not AppContainer): {path.name}")
    return record


def main():
    """Walk the staged closure and required assets, rejecting defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--kind", required=True, choices=("runtime", "minibrowser", "engine"),
                        help="runtime: RuntimeSmoke host+payload; minibrowser: MiniBrowser app; "
                             "engine: engine-regression stage (host base + engine libs + fixture CA)")
    parser.add_argument("--reader", default="llvm-readobj")
    parser.add_argument("--search", action="append", default=[], type=Path,
                        help="Extra directories resolving closure DLLs (same role as audit-dlls --search)")
    parser.add_argument("--engine-build", type=Path, default=None,
                        help="Engine CMake build dir; when its CMakeCache.txt exists, "
                             "--require-skia additionally demands USE_SKIA:BOOL=ON there")
    parser.add_argument("--require-skia", dest="require_skia", action="store_true", default=None,
                        help="Reject candidates without the Skia GL payload (default for minibrowser/engine)")
    parser.add_argument("--no-require-skia", dest="require_skia", action="store_false",
                        help="Allow a non-Skia engine candidate")
    parser.add_argument("--require-ca", action="append", default=[],
                        help="Extra staged CA/asset filename required (repeatable)")
    parser.add_argument("--require-blocked", action="store_true", default=None,
                        help="Require the demo/blocked navigation-error template (default for minibrowser)")
    parser.add_argument("--no-require-blocked", dest="require_blocked", action="store_false")
    parser.add_argument("--expect-winmd", action="append", default=[],
                        help="Staged component WinMD filename required (repeatable)")
    parser.add_argument("--appcontainer-exempt", action="append", default=list(DEFAULT_APPCONTAINER_EXEMPT),
                        help="Staged image basenames waived from the AppContainer flag "
                             "(default: icudt78.dll, ICU data-only payload; recorded in the report)")
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    require_skia = args.require_skia if args.require_skia is not None else args.kind in ("minibrowser", "engine")
    require_blocked = args.require_blocked if args.require_blocked is not None else args.kind == "minibrowser"

    rejects = []
    stage = args.stage
    if not stage.is_dir():
        parser.error(f"Staging directory not found: {stage}")

    manifest_path = stage / "AppxManifest.xml"
    if not manifest_path.is_file():
        rejects.append("REJECT: missing asset: AppxManifest.xml")
        manifest_exe = None
    else:
        try:
            root = ET.parse(manifest_path).getroot()
            application = root.find(f"{{{FOUNDATION}}}Applications/{{{FOUNDATION}}}Application")
            manifest_exe = application.get("Executable") if application is not None else None
        except ET.ParseError as failure:
            rejects.append(f"REJECT: AppxManifest.xml unparsable: {failure}")
            manifest_exe = None

    # Closure roots per kind. Missing roots are defects, not skips: a stage
    # without its host, payload, or engine cannot become a package.
    roots = []
    if manifest_exe:
        roots.append(stage / manifest_exe)
    if args.kind == "runtime":
        roots.append(stage / "WebKitRuntimeSmoke.dll")
    for name in ENGINE_LIBS:
        if args.kind in ("runtime", "minibrowser"):
            roots.append(stage / name)
    if args.kind == "engine":
        roots.extend(stage / name for name in ENGINE_LIBS + SKIA_LIBS)
    for root in roots:
        if not root.is_file():
            rejects.append(f"REJECT: missing staged binary: {root.name}")

    available = {}
    for directory in (stage, *args.search):
        if directory.is_dir():
            for path in directory.glob("*.dll"):
                available.setdefault(path.name.lower(), path)

    # Recursive import closure reusing audit-dlls.inspect (no duplicate walk).
    exempt = {name.lower() for name in args.appcontainer_exempt}
    seen = set()
    pending = [path.resolve() for path in roots if path.is_file()]
    closure = []
    external = set()
    while pending:
        path = pending.pop(0)
        if path in seen:
            continue
        seen.add(path)
        record = check_binary(path, args.reader, rejects,
                              require_appcontainer=path.name.lower() not in exempt)
        if record is None:
            continue
        closure.append(record)
        for dependency in record["imports"]:
            local = available.get(dependency["dll"].lower())
            if local:
                pending.append(local.resolve())
            else:
                external.add(dependency["dll"])

    # Strays outside the closure get the same ABI gate: every staged image
    # ships, so every staged image must be ARMNT/AppContainer/never-ARM64.
    audited_names = {Path(record["path"]).name.lower() for record in closure}
    for path in sorted(stage.glob("*.dll")) + sorted(stage.glob("*.exe")):
        if path.resolve() in seen:
            continue
        record = check_binary(path, args.reader, rejects,
                              require_appcontainer=path.name.lower() not in exempt)
        if record is not None:
            closure.append(record)
        audited_names.add(path.name.lower())

    # Required assets: fonts always; CA/demo/blocked per kind and overrides.
    for name in FONT_FILES:
        if not (stage / "Fonts" / name).is_file():
            rejects.append(f"REJECT: missing asset: Fonts/{name}")
    required_ca = list(args.require_ca)
    if args.kind == "engine":
        required_ca.extend(n for n in ENGINE_CA_FILES if n not in required_ca)
    for name in required_ca:
        if not (stage / name).is_file():
            rejects.append(f"REJECT: missing asset: {name}")
    if require_blocked and not (stage / BLOCKED_TEMPLATE).is_file():
        rejects.append(f"REJECT: missing asset: {BLOCKED_TEMPLATE}")

    # Skia gate: staged GL stack must be present and conforming; when the
    # engine build cache is available its backend selection must agree.
    if require_skia:
        for name in SKIA_LIBS:
            candidate = stage / name
            if not candidate.is_file():
                rejects.append(f"REJECT: non-Skia candidate (missing {name})")
            elif candidate.name.lower() not in audited_names:
                rejects.append(f"REJECT: non-Skia candidate (unaudited {name})")
        if args.engine_build is not None:
            cache = args.engine_build / "CMakeCache.txt"
            if cache.is_file():
                lines = cache.read_text(encoding="utf-8", errors="replace").splitlines()
                if "USE_SKIA:BOOL=ON" not in lines:
                    rejects.append("REJECT: non-Skia candidate "
                                   f"(engine cache lacks USE_SKIA:BOOL=ON: {cache})")
            else:
                rejects.append(f"REJECT: --engine-build has no CMakeCache.txt: {cache}")

    # Metadata gate: every staged WinMD must pair with its staged component
    # DLL (same stem, e.g. WebKit.winmd beside WebKit.dll); a stray WinMD is
    # a conflicting independently generated identity and is rejected.
    staged_winmds = sorted(stage.glob("*.winmd"))
    staged_dlls = {path.stem.lower() for path in stage.glob("*.dll")}
    for winmd in staged_winmds:
        if winmd.stem.lower() not in staged_dlls:
            rejects.append(f"REJECT: conflicting metadata without matching staged DLL: {winmd.name}")
    for name in args.expect_winmd:
        if not (stage / name).is_file():
            rejects.append(f"REJECT: missing expected component metadata: {name}")

    # Runtime payload exports (same contract the staging recipe asserts).
    if args.kind == "runtime":
        payload = stage / "WebKitRuntimeSmoke.dll"
        if payload.is_file():
            exports = subprocess.check_output(
                [args.reader, "--coff-exports", str(payload)],
                text=True, encoding="utf-8")
            names = {line.strip() for line in exports.splitlines()}
            for symbol in RUNTIME_EXPORTS:
                if f"Name: {symbol}" not in names:
                    rejects.append(f"REJECT: payload export missing: {symbol}")

    report = {"stage": str(stage.resolve()), "kind": args.kind,
              "require_skia": require_skia, "require_blocked": require_blocked,
              "appcontainer_exempt": sorted(exempt),
              "binaries": closure, "external_dependencies": sorted(external),
              "winmd": [path.name for path in staged_winmds],
              "rejects": rejects}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if rejects:
        for line in rejects:
            print(line)
        print(f"AUDIT REJECT: {len(rejects)} defect(s) in {stage}; no package may be created")
        raise SystemExit(1)
    print(f"AUDIT PASS: {len(closure)} ARMNT/AppContainer binaries, kind={args.kind}, "
          f"skia={'required+present' if require_skia else 'not-required'} in {stage}")
    print("Resolve through OS/VCLibs on device: " + ", ".join(sorted(external)))


if __name__ == "__main__":
    main()
