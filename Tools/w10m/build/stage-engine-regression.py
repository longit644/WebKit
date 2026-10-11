"""Stage a side-by-side phone regression package and record its exact payload hashes."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[3]
FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"
UAP = "http://schemas.microsoft.com/appx/manifest/uap/windows10"


def digest(path):
    """Identify staged bytes without loading a native image on the host."""
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    """Reuse the audited staging recipe, then verify matching engine outputs."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("runtime",), required=True,
                        help="Only the C# RuntimeSmoke host remains in this staging path")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--host-appx", type=Path,
                        help="C# RuntimeSmoke MSBuild appx (required for --kind runtime)")
    parser.add_argument("--payload-build", type=Path,
                        help="Native RuntimeSmoke CMake build (required for --kind runtime)")
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--reader", required=True)
    parser.add_argument("--version", default="1.0.0.1")
    parser.add_argument("--skip-audit", action="store_true",
                        help="Bypass the pre-package staging audit (task 10.4 gate; not for device evidence)")
    args = parser.parse_args()
    if args.stage.exists():
        parser.error("Use a new staging directory for each package build")

    if not args.host_appx or not args.payload_build:
        parser.error("--kind runtime requires --host-appx and --payload-build")
    command = [sys.executable,
               str(REPO / "Tools/w10m/diagnostics/RuntimeSmoke/scripts/stage-runtimesmoke.py"),
               "--host-appx", str(args.host_appx),
               "--payload-build", str(args.payload_build),
               "--engine", str(args.engine), "--prefix", str(args.prefix),
               "--stage", str(args.stage), "--reader", args.reader,
               "--override", str(args.baseline)]
    subprocess.run(command, check=True)

    tree = ET.parse(args.stage / "AppxManifest.xml")
    identity = tree.getroot().find(f"{{{FOUNDATION}}}Identity")
    name = "WebKitProbe.EngineRuntime"
    identity.set("Name", name)
    identity.set("Version", args.version)
    display = "M10 Engine Runtime"
    tree.getroot().find(f"{{{FOUNDATION}}}Properties/{{{FOUNDATION}}}DisplayName").text = display
    visual = tree.getroot().find(f".//{{{UAP}}}VisualElements")
    visual.set("DisplayName", display)
    ET.register_namespace("", FOUNDATION)
    ET.register_namespace("uap", UAP)
    tree.write(args.stage / "AppxManifest.xml", encoding="utf-8", xml_declaration=True)

    # Keep the four freshly built engine libraries together, even when a host's
    # static imports do not exercise all presentation paths.
    for filename in ("WebCore.dll", "JavaScriptCore.dll", "libEGL.dll", "libGLESv2.dll"):
        shutil.copy2(args.engine / filename, args.stage / filename)
        assert digest(args.stage / filename) == digest(args.engine / filename)
    # Fixture TLS certificate authorities for task 4.11 trust isolation. The
    # engine resolves them beside the host executable, never from the network.
    tls_directory = REPO / "Tools/w10m/NavigationTests/tls"
    for filename in ("fixture-ca.pem", "fixture-unrelated-ca.pem"):
        shutil.copy2(tls_directory / filename, args.stage / filename)
    if not args.skip_audit:
        # M10 task 10.4: reject negative ARM64, incompatible ABI, missing
        # asset and non-Skia candidates before any package identity exists.
        gate = [sys.executable, str(REPO / "Tools/w10m/build/audit-stage-package.py"),
                "--stage", str(args.stage), "--kind", "engine", "--reader", args.reader]
        subprocess.run(gate, check=True)
    payload = {path.relative_to(args.stage).as_posix(): digest(path)
               for path in sorted(args.stage.rglob("*")) if path.is_file()}
    report = {"identity": name, "version": args.version, "kind": args.kind,
              "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
              "build": str(args.build.resolve()), "engine": str(args.engine.resolve()),
              "baseline": str(args.baseline.resolve()), "files": payload}
    # Place the record beside the stage, outside the Appx payload itself.
    record = args.stage.parent / (args.stage.name + "-identity.json")
    record.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Identity: {name} {args.version}; record: {record}")


if __name__ == "__main__":
    main()
