OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
    rom   : ORIGIN = 0x08000000, LENGTH = 32M
    iwram : ORIGIN = 0x03000000, LENGTH = 32K - 0x100
    ewram : ORIGIN = 0x02000000, LENGTH = 256K
}
SECTIONS {
    .text : {
        KEEP(*(.crt0))
        *(EXCLUDE_FILE(*libgcc.a:_divsi3.o *libgcc.a:_udivsi3.o) .text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom
    .data : {
        __data_start = .;
        *(.iwram .iwram.*)
        *libgcc.a:_divsi3.o(.text .text.*)     /* division runs much faster from IWRAM */
        *libgcc.a:_udivsi3.o(.text .text.*)
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > iwram AT > rom
    __data_lma = LOADADDR(.data);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > iwram
    .ewram (NOLOAD) : {
        __ewram_start = .;
        *(.ewram .ewram.*)
        . = ALIGN(4);
        __ewram_end = .;
    } > ewram
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
