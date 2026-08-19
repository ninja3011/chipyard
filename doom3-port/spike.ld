SECTIONS {
  . = 0x80000000;
  .text : { *(.text*) }
  . = ALIGN(0x1000);
  .data : { *(.data*) }
  .bss : {
    *(.bss*)
    *(COMMON)
    . = ALIGN(0x1000);
  }
}
