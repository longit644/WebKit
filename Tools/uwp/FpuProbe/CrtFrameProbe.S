// Diagnostic tail calls to the real CRT, bypassing the experimental crtjmp.S.
// No stack adjustment: each frame argument describes the C++ caller.
    .syntax unified
    .thumb
    .text
    .macro CRT_FRAME_PROBE name, frame
    .globl \name
    .p2align 1
    .thumb_func
\name:
    mov r1, \frame
    movw r12, :lower16:wkProbeCrtSetjmp
    movt r12, :upper16:wkProbeCrtSetjmp
    ldr r12, [r12]
    bx r12
    .endm
    CRT_FRAME_PROBE wkProbeSetjmpSp, sp
    CRT_FRAME_PROBE wkProbeSetjmpR11, r11
