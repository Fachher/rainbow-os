/* Linker script for Rainbow-OS ring-3 user programs.
   Flat binary loaded and entered at 0x200000 (CG_LOAD_ADDR). */
ENTRY(_start)

SECTIONS
{
    . = 0x200000;

    .text : {
        *(.start)              /* crt0 _start must come first */
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    . = ALIGN(4);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
