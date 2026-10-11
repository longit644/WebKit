cmake_minimum_required(VERSION 3.24)
project(WKW10MNativeToolchainSmoke LANGUAGES C)

# Exercise the actual native compiler/header/link flag chain independently of WebCore.
add_library(WKW10MNativeToolchainSmoke SHARED NativeToolchainSmoke.c)
target_compile_features(WKW10MNativeToolchainSmoke PRIVATE c_std_11)

# A browser-free DLL proves clean ARM/native linking without needing an executable shim.
target_link_options(WKW10MNativeToolchainSmoke PRIVATE /NOENTRY)
