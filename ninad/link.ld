OUTPUT_ARCH( "riscv" )
ENTRY( _start )

MEMORY {
  /* TinyRocketConfig's DTIM (Rocket TCM) is 16KB, per the generated
     chipyard...TinyRocketConfig.dts: dtim@80000000 { reg = <0x80000000 0x4000>; }
     This is the *only* memory in this SoC -- there is no external RAM. */
  BRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16K
}

SECTIONS {
  .text : {
    /* Force the boot assembly to be the VERY FIRST thing in memory */
    *(.text.init)
    /* Followed by the rest of the C code */
    *(.text)
  } > BRAM
  
  .data : { *(.data) } > BRAM
  .bss : { *(.bss) } > BRAM
}
