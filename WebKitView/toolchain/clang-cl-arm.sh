#!/bin/sh
# WebKitView ARM32-UWP compiler wrapper for autotools ports (master copy).
# Live copy: C:/Users/Longi/llvm-tools/clang-cl-arm (extensionless, msys-exec).
# vcpkg/libtool wrap every flag in -Xcompiler/-Xlinker escapes, which clang-cl
# rejects ("unknown argument ignored" -> flags silently lost). This unwraps,
# maps -lfoo to foo.lib, translates dash-form link flags to slash-form,
# selects lld via PATH, and puts link flags after /link (clang-cl convention).
# Compile-only (-c/-E) lines get compile flags only. Target/flags mirror
# Toolchain-ARM32-UWP-clang.cmake (KEEP IN SYNC).
# NOTE: vcpkg's msys sh lacks ${var#pat} trimming; sed is used instead.
export PATH="/c/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/Llvm/bin:$PATH"
# msys converts /FOO args to POSIX paths (eating linker flags); exclude ours.
# Dash-args are never converted; C:/... drive paths pass through; /tmp/... must convert.
export MSYS2_ARG_CONV_EXCL="/link;/APPCONTAINER;/MACHINE;/machine;/SUBSYSTEM;/subsystem;/NODEFAULTLIB;/nodefaultlib;/LIBPATH;/libpath;/ENTRY;/entry;/INCREMENTAL;/incremental;/MANIFEST;/manifest;/DYNAMICBASE;/dynamicbase;/NXCOMPAT;/nxcompat;/ALTERNATENAME;/alternatename;/MD;/MT;/LD;-imsvc"
cc="C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/Llvm/bin/clang-cl.exe"
tgt="--target=thumbv7-unknown-windows-msvc"
compile_only=0
for a in "$@"; do
  case "$a" in
    -c|-E|-M|-showIncludes) compile_only=1 ;;
  esac
done
comp=""
link=""
for a in "$@"; do
  case "$a" in
    -Xcompiler|-Xlinker) continue ;;
    -B*|-fuse-ld=*) continue ;;
    -EHsc|-EHa*) continue ;;
    -l*) a=`echo "$a" | sed 's/^-l//'`
         a="$a.lib" ;;
  esac
  case "$a" in
    -APPCONTAINER|-MACHINE:*|-machine:*|-SUBSYSTEM:*|-subsystem:*|-NODEFAULTLIB:*|-nodefaultlib:*|-LIBPATH:*|-libpath:*|-ENTRY:*|-entry:*|-INCREMENTAL*|-incremental*|-MANIFEST*|-manifest*|-DYNAMICBASE*|-dynamicbase*|-NXCOMPAT*|-nxcompat*|-ALTERNATENAME*|-alternatename*)
      a=`echo "$a" | sed 's/^-/\//'` ;;
  esac
  case "$a" in
    /APPCONTAINER|/MACHINE:*|/machine:*|/SUBSYSTEM:*|/subsystem:*|/NODEFAULTLIB:*|/nodefaultlib:*|/LIBPATH:*|/libpath:*|/ENTRY:*|/entry:*|/INCREMENTAL*|/incremental*|/MANIFEST*|/manifest*|/ALTERNATENAME*|/alternatename*|*.lib|*.Lib|*.LIB)
      link="$link \"$a\""
      continue ;;
  esac
  comp="$comp \"$a\""
done
if [ "$compile_only" = "1" ]; then
  eval "exec \"$cc\" $tgt -fuse-ld=lld $comp"
else
  eval "exec \"$cc\" $tgt -fuse-ld=lld $comp /link $link"
fi
