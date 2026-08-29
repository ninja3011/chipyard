/*
 * Links the Freedoom Phase 1 IWAD directly into the ELF as read-only data,
 * since there is no filesystem in this bare-metal environment. The WAD file
 * I/O shim in syscalls.c serves reads against this embedded blob instead of
 * a real file.
 *
 * Note: this WAD is 28.8MB. Loading a linked ELF this size over uart_tsi
 * (the documented Arty100T bring-up transport, ~90KB/s at its fastest
 * baud rate) takes on the order of 5 minutes per load during bring-up.
 * That's a real cost worth knowing going in, not a bug — SD-card boot
 * (Tier 2 in ARTY-A7-100T-PROJECT-SCOPE.md) is the eventual fix, not
 * something to solve for this bare-metal bring-up.
 */
  .section .rodata.wad, "a"
  .global doom_wad_start
  .global doom_wad_end
  .align 4
doom_wad_start:
  .incbin "freedoom1.wad"
doom_wad_end:
