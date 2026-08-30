/*
 * Links a .vidf video file (see software/video/video2frames.py) directly
 * into the ELF, same reasoning as wad_embed.S: no filesystem here.
 */
  .section .rodata.vidf, "a"
  .global video_vidf_start
  .global video_vidf_end
  .align 4
video_vidf_start:
  .incbin VIDF_FILE
video_vidf_end:
