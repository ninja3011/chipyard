ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 512M
}

SECTIONS {
    .text 0x80000000 : {
        *(.text)
        *(.text.*)
    } > RAM
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > RAM
    
    .data : {
        *(.data)
        *(.data.*)
    } > RAM
    
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } > RAM
    
    /DISCARD/ : {
        *(.note.GNU-stack)
    }
}
