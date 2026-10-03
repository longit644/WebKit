// ARM MSVC division ABI: denominator first; quotient in r0/r1 and remainder
// in r2/r3 (64-bit), or quotient r0 and remainder r1 (32-bit).
    .syntax unified
    .thumb
    .text
    .macro DIV64 name, helper
    .globl \name
    .p2align 1
    .thumb_func
\name:
    .seh_proc \name
    push {r4, lr}
    .seh_save_regs {r4, lr}
    sub sp, sp, #16
    .seh_stackalloc 16
    .seh_endprologue
    mov r4, r0
    mov r0, r2
    mov r2, r4
    mov r4, r1
    mov r1, r3
    mov r3, r4
    add r4, sp, #8
    str r4, [sp]
    bl \helper
    ldr r2, [sp, #8]
    ldr r3, [sp, #12]
    .seh_startepilogue
    add sp, sp, #16
    .seh_stackalloc 16
    pop {r4, pc}
    .seh_save_regs {r4, lr}
    .seh_endepilogue
    .seh_endproc
    .endm
    DIV64 __rt_sdiv64, __divmoddi4
    DIV64 __rt_udiv64, __udivmoddi4

    .macro DIV32 name, helper
    .globl \name
    .p2align 1
    .thumb_func
\name:
    .seh_proc \name
    push {r4, lr}
    .seh_save_regs {r4, lr}
    sub sp, sp, #8
    .seh_stackalloc 8
    .seh_endprologue
    mov r4, r0
    mov r0, r1
    mov r1, r4
    mov r2, sp
    bl \helper
    ldr r1, [sp]
    .seh_startepilogue
    add sp, sp, #8
    .seh_stackalloc 8
    pop {r4, pc}
    .seh_save_regs {r4, lr}
    .seh_endepilogue
    .seh_endproc
    .endm
    DIV32 __rt_sdiv, __divmodsi4
    DIV32 __rt_udiv, __udivmodsi4
