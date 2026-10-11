#!/usr/bin/env python3
"""Stage the native runtime probe and its engine dependency closure for MakeAppx."""
import argparse
from pathlib import Path
import shutil
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from importlib import import_module
audit = import_module("audit-dlls")

# PNG payload values and package logo dimensions used by the generated placeholders.
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_BIT_DEPTH = 8
PNG_COLOR_TYPE_RGBA = 6
PNG_NO_COMPRESSION_FILTER_INTERLACE = (0, 0, 0)
LOGO_RGBA = (32, 96, 160, 255)
LOGO_SIZES = (44, 50, 150)
BLOCKED_TEMPLATE_FILENAME = "blocked.html"


def logo(size):
    """Render a solid placeholder logo PNG of the given pixel size."""
    def chunk(kind, content):
        """Wrap one PNG chunk with length and CRC."""
        return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content))
    pixels = (b"\0" + bytes(LOGO_RGBA) * size) * size
    return (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, PNG_BIT_DEPTH, PNG_COLOR_TYPE_RGBA, *PNG_NO_COMPRESSION_FILTER_INTERLACE))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def main():
    """Stage the app closure (binary, engine, fonts, manifest) for MakeAppx."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--reader", default="llvm-readobj")
    parser.add_argument("--override", action="append", default=[], type=Path, help="Prefer rebuilt dependency DLLs from this directory")
    parser.add_argument("--host", default="WebKitRuntimeSmokeHost.exe")
    parser.add_argument("--no-probe", action="store_true")
    parser.add_argument("--manifest", type=Path, required=True,
                        help="Explicit host manifest; application manifests are owned by each host project")
    args = parser.parse_args()
    if args.host.lower() == "webkitminibrowser.exe":
        engine_cache = args.engine.parent / "CMakeCache.txt"
        if not engine_cache.is_file() or "USE_SKIA:BOOL=ON" not in engine_cache.read_text().splitlines():
            parser.error("MiniBrowser requires USE_SKIA=ON; use --engine build-arm-w10m-skia/bin (see Tools/w10m/SKIA-PORT.md)")
    paths = [args.build / args.host, args.engine / "WebCore.dll"]
    if not args.no_probe:
        paths.append(args.build / "WebKitRuntimeSmoke.dll")
    available = {}
    for directory in (args.build, args.engine, *args.override, args.prefix / "bin"):
        for path in directory.glob("*.dll"):
            available.setdefault(path.name.lower(), path)
    seen = set()
    external = set()
    args.stage.mkdir(parents=True, exist_ok=True)
    while paths:
        path = paths.pop(0)
        if path.name.lower() in seen:
            continue
        seen.add(path.name.lower())
        record = audit.inspect(path, args.reader)
        if not record["armnt"]:
            raise RuntimeError(f"Not ARMNT: {path}")
        shutil.copy2(path, args.stage / path.name)
        for dependency in record["imports"]:
            local = available.get(dependency["dll"].lower())
            if local:
                paths.append(local)
            else:
                external.add(dependency["dll"])
    assets = args.stage / "Assets"
    assets.mkdir(exist_ok=True)
    for size in LOGO_SIZES:
        (assets / f"Logo{size}.png").write_bytes(logo(size))
    shutil.copy2(args.manifest, args.stage / "AppxManifest.xml")
    ca_bundle = args.manifest.parent / "cacert.pem"
    if ca_bundle.is_file():
        shutil.copy2(ca_bundle, args.stage / ca_bundle.name)
    # MiniBrowser's built-in navigation error page is a packaged, overridable template.
    blocked_template = args.manifest.parent / BLOCKED_TEMPLATE_FILENAME
    if blocked_template.is_file():
        shutil.copy2(blocked_template, args.stage / BLOCKED_TEMPLATE_FILENAME)
    fonts = args.stage / "Fonts"
    fonts.mkdir(exist_ok=True)
    source_fonts = Path(__file__).resolve().parents[2] / "WebKitTestRunner" / "glib" / "fonts"
    for name in ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "LICENSE.dejavu"):
        shutil.copy2(source_fonts / name, fonts / name)
    print(f"Staged {len(seen)} ARM binaries in {args.stage}")
    print("Resolve through OS/VCLibs on device: " + ", ".join(sorted(external)))


if __name__ == "__main__":
    main()
