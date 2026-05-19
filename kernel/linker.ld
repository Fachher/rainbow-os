/* Rainbow-OS Kernel Linker Script */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;   /* Kernel loaded at 1 MB */

    .text ALIGN(4K) :
    {
        *(.text)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
