@ Minimal GBA startup: ROM header, stack setup, .data/.bss init, jump to main.
    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       reset
    .fill   156, 1, 0           @ Nintendo logo area (filled in by tools/fixheader.py)
    .ascii  "NO TRACIN'  "      @ 0xA0 title (12 bytes)
    .ascii  "NTRC"              @ 0xAC game code
    .ascii  "00"                @ 0xB0 maker code
    .byte   0x96                @ 0xB2 fixed value
    .byte   0x00                @ 0xB3 unit code
    .byte   0x00                @ 0xB4 device type
    .fill   7, 1, 0             @ 0xB5 reserved
    .byte   0x00                @ 0xBC version
    .byte   0x00                @ 0xBD header complement (set by tools/fixheader.py)
    .fill   2, 1, 0             @ 0xBE reserved

reset:
    mov     r0, #0x12           @ IRQ mode stack
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F           @ System mode stack
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    ldr     r0, =__data_lma     @ copy .data from ROM to IWRAM
    ldr     r1, =__data_start
    ldr     r2, =__data_end
1:  cmp     r1, r2
    ldrlt   r3, [r0], #4
    strlt   r3, [r1], #4
    blt     1b

    ldr     r1, =__bss_start    @ zero .bss
    ldr     r2, =__bss_end
    mov     r3, #0
2:  cmp     r1, r2
    strlt   r3, [r1], #4
    blt     2b

    ldr     r0, =main
    bx      r0
    .pool
