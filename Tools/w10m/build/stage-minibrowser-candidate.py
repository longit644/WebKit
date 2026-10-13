#!/usr/bin/env python3
"""Assemble an explicitly opted-in M11.0 HostHtml MiniBrowser phone candidate.

The engine must have been built with ``WKW10M_HOSTHTML_CANDIDATE=ON``. This
stager unpacks the signed C# shell package, adds the recursive ARM engine closure
and assets, audits it before repacking, signs the final AppX and records exact
payload/build identities beside the package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[3]
APPX_METADATA = {"AppxBlockMap.xml", "[Content_Types].xml",
                 "AppxSignature.p7x", "AppxMetadata/CodeIntegrity.cat"}


def sha256(path):
    """Hash a staged file for the package identity record."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def run(command):
    """Run one pack/sign/audit tool and preserve its failure status."""
    subprocess.run([str(argument) for argument in command], check=True)


def validate_dependency_baseline(directory, identity_path):
    """Reject missing or changed repaired DLLs before any package is staged."""
    identity = json.loads(identity_path.read_text(encoding="utf-8-sig"))
    hashes = identity.get("files", identity.get("payload_sha256", {}))
    dlls = sorted(directory.glob("*.dll"))
    required = {"freetype.dll", "fontconfig-1.dll", "icuuc78.dll", "icuin78.dll",
                "icudt78.dll", "harfbuzz.dll", "harfbuzz-icu.dll"}
    if not required.issubset({path.name.lower() for path in dlls}):
        raise ValueError("Dependency baseline is missing the repaired font/text DLL closure")
    for path in dlls:
        if hashes.get(path.name) != sha256(path):
            raise ValueError(f"Dependency baseline DLL differs from its identity: {path}")
    return identity


def main():
    """Build the staged package and its content-addressed identity record."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-appx", required=True, type=Path)
    parser.add_argument("--engine-build", required=True, type=Path,
                        help="Configured native build containing WebCore.dll and the candidate CMake cache")
    parser.add_argument("--prefix", required=True, type=Path,
                        help="ARM vcpkg install prefix used to resolve imported engine DLLs")
    parser.add_argument("--dependency-baseline", required=True, type=Path,
                        help="Phone-tested dependency stage, preferred over the stale vcpkg DLLs")
    parser.add_argument("--baseline-identity", required=True, type=Path,
                        help="Identity JSON recording the baseline's DLL hashes in files or payload_sha256")
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output-appx", required=True, type=Path)
    parser.add_argument("--version", required=True,
                        help="Monotonic candidate package version (four numeric fields)")
    parser.add_argument("--reader", default="llvm-readobj")
    parser.add_argument("--makeappx", required=True, type=Path)
    parser.add_argument("--signtool", required=True, type=Path)
    parser.add_argument("--thumbprint", required=True)
    args = parser.parse_args()

    engine = args.engine_build / "bin"
    identity_path = args.engine_build.parent / "m10-build-identity.json"
    host_identity_path = args.host_appx.parent / "build-identity.json"
    cache_path = args.engine_build / "CMakeCache.txt"
    for required in (args.host_appx, engine / "WebCore.dll", engine / "JavaScriptCore.dll",
                     identity_path, host_identity_path, cache_path, args.baseline_identity):
        if not required.is_file():
            parser.error(f"Missing candidate input: {required}")
    if args.stage.exists() or args.output_appx.exists():
        parser.error("Use fresh --stage and --output-appx paths for every candidate")

    identity = json.loads(identity_path.read_text(encoding="utf-8-sig"))
    if not identity.get("HostHtmlCandidate"):
        parser.error("Engine identity does not identify a HostHtml candidate build")
    host_identity = json.loads(host_identity_path.read_text(encoding="utf-8-sig"))
    if not host_identity.get("HostHtmlSmoke"):
        parser.error("Host package was not built with the automatic HostHtml smoke command")
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", args.version):
        parser.error("--version must contain four numeric fields")
    cache = cache_path.read_text(encoding="utf-8", errors="replace").splitlines()
    if "WKW10M_HOSTHTML_CANDIDATE:BOOL=ON" not in cache:
        parser.error("Engine CMake cache did not enable WKW10M_HOSTHTML_CANDIDATE")

    try:
        baseline_identity = validate_dependency_baseline(args.dependency_baseline, args.baseline_identity)
    except (ValueError, OSError) as error:
        parser.error(str(error))

    host_dir = args.stage.parent / (args.stage.name + "-host")
    if host_dir.exists():
        parser.error(f"Use a fresh host extraction directory: {host_dir}")
    host_dir.mkdir(parents=True)
    with zipfile.ZipFile(args.host_appx) as package:
        names = package.namelist()
        if "WebKitMiniBrowser.exe" not in names or "AppxManifest.xml" not in names:
            parser.error("Host AppX is missing its executable or manifest")
        package.extract("WebKitMiniBrowser.exe", host_dir)
        package.extract("AppxManifest.xml", host_dir)
        if "Assets/blocked.html" in names:
            (host_dir / "blocked.html").write_bytes(package.read("Assets/blocked.html"))
    # Candidate initialization validates its configured bundle before creating a page.
    # Remote/TLS is not exercised by this local-only probe; this fixture CA is packaged
    # only to satisfy the immutable per-page configuration contract.
    fixture_ca = ROOT / "Tools/w10m/NavigationTests/tls/fixture-ca.pem"
    (host_dir / "cacert.pem").write_bytes(fixture_ca.read_bytes())

    stage_recipe = ROOT / "Tools/w10m/RuntimeSmoke/stage-app.py"
    run([sys.executable, stage_recipe, "--build", host_dir, "--host", "WebKitMiniBrowser.exe",
         "--manifest", host_dir / "AppxManifest.xml", "--engine", engine,
         "--prefix", args.prefix, "--override", args.dependency_baseline,
         "--stage", args.stage, "--reader", args.reader, "--no-probe"])

    # Keep the MSBuild-owned component, compiled XAML, PRI and shell resources intact.
    with zipfile.ZipFile(args.host_appx) as package:
        for name in names:
            if name in APPX_METADATA or name in ("AppxManifest.xml", "Assets/blocked.html"):
                continue
            target = args.stage / Path(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            with package.open(name) as source, target.open("wb") as output:
                output.write(source.read())

    # The audit checks architecture and closure; separately require identity
    # equality for the four configured engine outputs to prevent mixed builds.
    for name in ("WebCore.dll", "JavaScriptCore.dll", "libEGL.dll", "libGLESv2.dll"):
        if sha256(args.stage / name) != sha256(engine / name):
            parser.error(f"Staged engine output differs from the configured build: {name}")

    # Re-versioning the unchanged diagnostic host allows repeated candidate
    # deployments without requiring a full .NET Native rebuild each time.
    manifest_path = args.stage / "AppxManifest.xml"
    manifest = manifest_path.read_text(encoding="utf-8-sig")
    versioned, replacements = re.subn(
        r'(<Identity\b[^>]*\bVersion=")[^"]+("[^>]*>)',
        lambda match: match.group(1) + args.version + match.group(2), manifest, count=1)
    if replacements != 1:
        parser.error("Packaged AppxManifest.xml does not have one identity version to update")
    manifest_path.write_text(versioned, encoding="utf-8")

    run([sys.executable, ROOT / "Tools/w10m/build/audit-stage-package.py",
         "--stage", args.stage, "--kind", "minibrowser", "--reader", args.reader,
         "--engine-build", args.engine_build])
    args.output_appx.parent.mkdir(parents=True, exist_ok=True)
    run([args.makeappx, "pack", "/d", args.stage, "/p", args.output_appx, "/o"])
    run([args.signtool, "sign", "/fd", "SHA256", "/sha1", args.thumbprint, args.output_appx])
    run([args.signtool, "verify", "/pa", args.output_appx])

    payload = {path.relative_to(args.stage).as_posix(): sha256(path)
               for path in sorted(args.stage.rglob("*")) if path.is_file()}
    report = {
        "identity": "WebKitProbe.MiniBrowser.HostHtmlCandidate",
        "package_version": args.version,
        "host_appx_sha256": sha256(args.host_appx),
        "host_build_identity": host_identity,
        "candidate_appx_sha256": sha256(args.output_appx),
        "engine_identity": identity,
        "dependency_baseline": str(args.dependency_baseline.resolve()),
        "baseline_identity_sha256": sha256(args.baseline_identity),
        "baseline_identity": baseline_identity,
        "stage": str(args.stage.resolve()),
        "package": str(args.output_appx.resolve()),
        "payload_sha256": payload,
    }
    record = args.output_appx.with_suffix(".identity.json")
    record.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"HostHtml candidate staged and signed: {args.output_appx}")
    print(f"Exact identity record: {record}")


if __name__ == "__main__":
    main()
