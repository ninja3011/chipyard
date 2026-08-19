/* Bare-metal linker script for DOOM on RISC-V BOOM
 * Memory layout:
 * 0x80000000 - Boot ROM + code (16MB)
 * 0x81000000 - Data + BSS (16MB)
 * 0x82000000 - Stack grows down from here (rest of memory)
 */

MEMORY
{
    code   (rx)  : ORIGIN = 0x80000000, LENGTH = 16M
    data   (rwx) : ORIGIN = 0x81000000, LENGTH = 16M
    bss    (rwx) : ORIGIN = 0x82000000, LENGTH = 256M
}

SECTIONS
{
    /* Text section - code and read-only data */
    .text : {
        *(.text.entry)
        *(.text*)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
    } > code

    /* Data section - initialized data */
    .data : {
        __data_start = .;
        *(.data*)
        __data_end = .;
    } > data AT > code

    /* BSS section - uninitialized data */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > bss

    /* HTIF interface for test harness communication */
    .tohost : {
        tohost = .;
        LONG(0);
        LONG(0);
    } > bss

    .fromhost : {
        fromhost = .;
        LONG(0);
        LONG(0);
    } > bss

    /* Stack at top of memory (grows down) */
    . = 0xA0000000;
    .stack : {
        PROVIDE(__stack_top = .);
    } > bss

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.note.*)
        *(.comment*)
    }
}

/* Symbols for initialization */
__text_start = ADDR(.text);
__text_end = ADDR(.text) + SIZEOF(.text);
__data_load_start = LOADADDR(.data);
__stack_pointer = 0xA0000000;
