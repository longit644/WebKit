#!/usr/bin/env python3
"""Stage the migrated RuntimeSmoke package: C# host plus native payload closure.

Unpacks the MSBuild-produced test appx as the packaging-correct managed base
(exe stub, compiled image, resources, real logos, framework-aware manifest),
then runs the audited shared staging recipe
(Tools/uwp/RuntimeSmoke/stage-app.py, unmodified) for the native payload and
engine dependency closure plus fonts, and overlays the appx base last so the
managed content stays exactly as MSBuild packaged it. The result is a signable
package layout; the caller packs it with MakeAppx.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(REPO / "Tools/uwp"))
from importlib import import_module

audit = import_module("audit-dlls")

FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"

# MSBuild packaging records that must not leak into the resigned layout.
APPX_METADATA = {"AppxBlockMap.xml", "[Content_Types].xml",
                 "AppxSignature.p7x", "AppxMetadata/CodeIntegrity.cat"}


def main():
    """Assemble the package layout and validate its contract."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-appx", required=True, type=Path,
                        help="MSBuild ARM Release test appx")
    parser.add_argument("--payload-build", required=True, type=Path,
                        help="Native payload CMake build directory")
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--reader", default="llvm-readobj")
    parser.add_argument("--override", action="append", default=[], type=Path)
    args = parser.parse_args()

    payload_dll = args.payload_build / "WebKitRuntimeSmoke.dll"
    for path in (args.host_appx, payload_dll, args.engine / "WebCore.dll"):
        if not path.is_file():
            parser.error(f"Missing required input: {path}")
    if args.stage.exists():
        parser.error("Use a new staging directory for each package build")

    # The shared recipe stages from one build directory, so merge the
    # packaging-correct host exe with the fresh payload DLL beside (never
    # inside) the stage.
    srcdir = args.stage.parent / (args.stage.name + "-srcdir")
    if srcdir.exists():
        parser.error("Use a new staging directory for each package build")
    srcdir.mkdir(parents=True)
    with zipfile.ZipFile(args.host_appx) as package:
        names = package.namelist()
        assert "WebKitRuntimeSmokeHost.exe" in names, names
        assert "AppxManifest.xml" in names, names
        package.extract("WebKitRuntimeSmokeHost.exe", srcdir)
        package.extract("AppxManifest.xml", srcdir)
    shutil.copy2(payload_dll, srcdir / payload_dll.name)

    command = [sys.executable, str(REPO / "Tools/uwp/RuntimeSmoke/stage-app.py"),
               "--build", str(srcdir), "--host", "WebKitRuntimeSmokeHost.exe",
               "--manifest", str(srcdir / "AppxManifest.xml"),
               "--engine", str(args.engine), "--prefix", str(args.prefix),
               "--stage", str(args.stage), "--reader", args.reader]
    for override in args.override:
        command += ["--override", str(override)]
    subprocess.run(command, check=True)

    # Overlay the MSBuild packaging base last: exe stub, compiled image,
    # resources, exact-size logos and the framework-aware manifest.
    with zipfile.ZipFile(args.host_appx) as package:
        for name in names:
            if name in APPX_METADATA:
                continue
            target = args.stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with package.open(name) as source, open(target, "wb") as output:
                shutil.copyfileobj(source, output)

    # Validate the staged contract before the caller packs it.
    manifest = ET.parse(args.stage / "AppxManifest.xml")
    identity = manifest.getroot().find(f"{{{FOUNDATION}}}Identity")
    assert identity.get("Name") == "WebKitWebView.RuntimeSmoke", identity.get("Name")
    assert identity.get("Version") == "1.0.0.11", identity.get("Version")
    assert identity.get("ProcessorArchitecture") == "arm", identity.get("ProcessorArchitecture")
    application = manifest.getroot().find(f"{{{FOUNDATION}}}Applications/{{{FOUNDATION}}}Application")
    assert application.get("Executable") == "WebKitRuntimeSmokeHost.exe", application.get("Executable")
    assert application.get("EntryPoint") == "WebKitWebView.RuntimeSmoke.App", application.get("EntryPoint")
    assets = args.stage / "Assets"
    for size in (44, 50, 150):
        data = (assets / f"Logo{size}.png").read_bytes()
        width, height = struct.unpack(">II", data[16:24])
        assert (width, height) == (size, size), (size, width, height)
    count = 0
    for path in sorted(args.stage.glob("*.dll")) + sorted(args.stage.glob("*.exe")):
        record = audit.inspect(path, args.reader)
        assert record["armnt"], f"Not ARMNT: {path}"
        count += 1
    exports = subprocess.check_output(
        [args.reader, "--coff-exports", str(args.stage / "WebKitRuntimeSmoke.dll")],
        text=True)
    for name in ("WKRunRuntimeSmoke", "WKRunSmokeDiagnostics"):
        assert any(line.strip() == f"Name: {name}"
                   for line in exports.splitlines()), f"Payload export missing: {name}"
    print(f"Staged {count} ARM binaries plus package content in {args.stage}")


if __name__ == "__main__":
    main()
