# Tasks — wkview-m1-host-jsc

- [x] Agent: run host JSC CLoop Release build (`build-jsc --cloop`), capture log tail
- [x] Agent: run `jsc` smoke script, record output + build time/RAM
- [x] Agent: commit log notes, push branch
- [ ] Owner: ack (no device needed); agent tags `wkview-m1-jsc`, opens M2 proposal

Build notes: configure needed VCPKG_ROOT=C:\vcpkg (set AFTER vcvarsall),
VS clang-cl 19.15 as compiler, -DCLANG_BUILTINS_LIBRARY to scoop LLVM23 lib,
-DVCPKG_MANIFEST_FEATURES=web;skia, -DENABLE_SAMPLING_PROFILER=OFF,
-DENABLE_WEBASSEMBLY=OFF, -DUSE_LCMS=OFF -DUSE_JPEGXL=OFF -DUSE_WOFF2=OFF,
linker flags /INCREMENTAL:NO (CMake 3.31 empty-var fix),
-DCMAKE_C/CXX_FLAGS=-Wno-cast-function-type-mismatch, gperf via
C:\vcpkg x64-windows tools, icu+curl in VS-vcpkg manifest tree.
Smoke: `jsc -e "print('wkview-m1 '+(1+2))"` -> `wkview-m1 3`, regex true.
Binary: WebKitBuild/Release/bin/jsc.exe (343 KB, gitignored build output).
