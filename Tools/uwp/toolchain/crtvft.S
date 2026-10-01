// WebKitWebView type_info vftable dummy (ARM32 Store CRT lacks a native one;
// the only copy lives in msvcurt's managed ti_inst.obj, which lld cannot
// parse). The reference comes from /GR- dynamic_cast lowering; our
// __RTDynamicCast stub (crrtti.c) ignores its typeinfo arguments, so this
// pointer is never dereferenced. See toolchain/crrtti.c.
    .syntax unified
    .text
    .globl "??_7type_info@@6B@"
    .data
    .align 2
"??_7type_info@@6B@":
    .long 0
