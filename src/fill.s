@ Span fill for Mode 4, run from IWRAM as ARM code.
@
@ void hspan(volatile u16 *p, s32 count, u32 value)
@   Stores `count` (> 0) halfwords of `value` (the pixel pair repeated in
@   both halves) starting at the halfword-aligned address p.

    .section .iwram, "ax", %progbits
    .arm
    .align 2
    .global hspan
hspan:
    tst     r0, #2                  @ align to a word boundary
    strneh  r2, [r0], #2
    subne   r1, r1, #1
    subs    r1, r1, #16             @ 16 halfwords = one 8-register store
    blt     2f
    push    {r4-r9}
    mov     r3, r2
    mov     r4, r2
    mov     r5, r2
    mov     r6, r2
    mov     r7, r2
    mov     r8, r2
    mov     r9, r2
1:  stmia   r0!, {r2-r9}
    subs    r1, r1, #16
    bge     1b
    pop     {r4-r9}
2:  @ r1 = remainder - 16; its low four bits are the remainder (0..15).
    mov     r3, r2
    tst     r1, #8
    stmneia r0!, {r2, r3}
    stmneia r0!, {r2, r3}
    tst     r1, #4
    stmneia r0!, {r2, r3}
    tst     r1, #2
    strne   r2, [r0], #4
    tst     r1, #1
    strneh  r2, [r0]
    bx      lr
