#!/bin/sh
# WebKitView: satisfies ICU configure's "link.exe is not a valid linker" check
# (it greps `link --version` for GNU coreutils, which msys ships). This reports
# LLD instead. Real linking always goes through clang-cl-arm (CC), never here.
# Live copy: C:/Users/Longi/llvm-tools/link (same dir as ENV{CC}, committed
# master at Tools/UWP/toolchain/link.sh).
exec "C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/Llvm/bin/lld-link.exe" "$@"
