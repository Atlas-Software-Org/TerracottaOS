ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .text : {
        *(.text*)
    }

    . = 0x401000;
    .bss : {
        *(COMMON)
        *(.bss*)
    }

    . = 0x402000;
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }
}
