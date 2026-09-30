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
# NOTE: bare `sed` may resolve to Chocolatey sed (broken quoting) via
# inherited Windows PATH. Always use msys sed by absolute path below.
# msys converts /FOO args to POSIX paths (eating linker flags); exclude ours.
# Dash-args are never converted; C:/... drive paths pass through; /tmp/... must convert.
export MSYS2_ARG_CONV_EXCL="/link;/APPCONTAINER;/MACHINE;/machine;/SUBSYSTEM;/subsystem;/NODEFAULTLIB;/nodefaultlib;/LIBPATH;/libpath;/ENTRY;/entry;/INCREMENTAL;/incremental;/MANIFEST;/manifest;/DYNAMICBASE;/dynamicbase;/NXCOMPAT;/nxcompat;/ALTERNATENAME;/alternatename;/DLL;/dll;/NOENTRY;/noentry;/IMPLIB;/implib;/DEF;/def;/OUT;/out;/MD;/MT;/LD;-imsvc;-FI;-showIncludes;-Fo;-Fe;-Fd;-Fm;-Fp;-Fa;-FR;-Fr;-Fx"
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
is_dll=0
for a in "$@"; do
  case "$a" in
    *.la|-Fe*.dll|-Fe*.DLL) is_dll=1 ;;
  esac
done
# Expand -Wl,a,b into separate args first (clang-cl 19 VS build does not
# unpack -Wl, itself; verified: raw token reaches lld which ignores it).
args=""
for a in "$@"; do
  case "$a" in
    -Wl,*) rest=`echo "$a" | /usr/bin/sed 's/^-Wl,//;s/,/ /g'`; args="$args $rest" ;;
    *) args="$args \"$a\"" ;;
  esac
done
eval "set -- $args"
for a in "$@"; do
  case "$a" in
    -Xcompiler|-Xlinker) continue ;;
    -B*|-fuse-ld=*) continue ;;
    -l*) a=`echo "$a" | /usr/bin/sed 's/^-l//'`; a="$a.lib" ;;
  esac
  case "$a" in
    -APPCONTAINER|-MACHINE:*|-machine:*|-SUBSYSTEM:*|-subsystem:*|-NODEFAULTLIB:*|-nodefaultlib:*|-LIBPATH:*|-libpath:*|-ENTRY:*|-entry:*|-INCREMENTAL*|-incremental*|-MANIFEST*|-manifest*|-DYNAMICBASE*|-dynamicbase*|-NXCOMPAT*|-nxcompat*|-ALTERNATENAME*|-alternatename*|-DLL|-dll|-IMPLIB:*|-implib:*|-DEF:*|-def:*|-OUT:*|-out:*)
      a=`echo "$a" | /usr/bin/sed 's/^-/\//'` ;;
  esac
  case "$a" in
    /APPCONTAINER|/MACHINE:*|/machine:*|/SUBSYSTEM:*|/subsystem:*|/NODEFAULTLIB:*|/nodefaultlib:*|/LIBPATH:*|/libpath:*|/ENTRY:*|/entry:*|/INCREMENTAL*|/incremental*|/MANIFEST*|/manifest*|/ALTERNATENAME*|/alternatename*|/DLL|/dll|/IMPLIB:*|/implib:*|/DEF:*|/def:*|/OUT:*|/out:*|*.lib|*.Lib|*.LIB)
      # Strip embedded quotes and turn ALL backslashes to / (libtool passes
      # single-backslash paths like -IMPLIB:".libs\x.lib", which sh/lld eat
      # as escapes). Safe: link tokens never carry meaningful backslash
      # escapes. (comp side untouched: -D"..." values need theirs.)
      a=`echo "$a" | /usr/bin/sed 's/"//g;s/\\/\//g'`
      link="$link \"$a\""
      continue ;;
  esac
  comp="$comp \"$a\""
done
if [ "$compile_only" = "1" ]; then
  eval "exec \"$cc\" $tgt -fuse-ld=lld -FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h $comp"
elif [ "$is_dll" = "1" ]; then
  # libtool .la shared libs: entry resolves via our arm-crtstart
  # (_DllMainCRTStartup -> default DllMain in its own lazy member).
  # No /NOENTRY: lld skips the import lib with it.
  eval "exec \"$cc\" $tgt -fuse-ld=lld -FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h $comp /link /DLL $link"
else
  eval "exec \"$cc\" $tgt -fuse-ld=lld -FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h $comp /link $link"
fi
