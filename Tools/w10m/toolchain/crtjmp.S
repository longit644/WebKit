// Adapt clang 19's Windows ARM setjmp frame argument to VCRUNTIME140.
//
// clang passes r11 as __intrinsic_setjmp's hidden second argument. On this
// Windows ARM runtime, longjmp's unwind target must instead be the caller's
// SP. FpuProbe 1.0.0.10 calls the original CRT explicitly: passing SP returns
// successfully; passing r11 raises STATUS_BAD_STACK (0xc0000028).
//
// This is a leaf tail adapter: SP is still the C/C++ caller's SP. The original
// CRT saves the jump buffer and the original longjmp performs Windows unwind
// and register restoration. Do not replace longjmp with a direct branch.
//
// arm-crtstart.lib precedes msvcurt.lib. The public symbol resolves here while
// the explicit __imp___intrinsic_setjmp reference resolves to the CRT IAT.
    .syntax unified
    .thumb
    .text
    .globl __intrinsic_setjmp
    .globl _setjmp
    .p2align 1
    // Compiler intrinsic spelling used by clang's Windows ARM setjmp lowering.
    .thumb_func
__intrinsic_setjmp:
    // Legacy CRT spelling shares the same caller-SP adapter.
    .thumb_func
_setjmp:
    mov r1, sp
    movw r12, :lower16:__imp___intrinsic_setjmp
    movt r12, :upper16:__imp___intrinsic_setjmp
    ldr r12, [r12]
    bx r12
