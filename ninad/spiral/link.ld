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
  .bss : {
    /* Exported so boot.S can zero this region explicitly. The DTIM is real
       SRAM: a CPU reset does not clear memory contents, and a previously
       loaded program may have left non-zero bytes here. A real crt0 always
       zeroes .bss before main() runs -- our bare boot.S didn't, which is
       exactly what let Game of Life boot with stale data from a prior
       program's run as its "generation 0" instead of an all-zero grid. */
    _bss_start = .;
    *(.bss)
    _bss_end = .;
  } > BRAM
}
