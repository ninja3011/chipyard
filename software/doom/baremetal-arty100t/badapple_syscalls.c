/*
 * Minimal newlib syscall stubs for the bare-metal Bad Apple player.
 * Unlike DOOM's syscalls.c, there's no WAD/file-I/O shim needed here --
 * the player reads the embedded .vidf directly via pointer arithmetic
 * (video_vidf_start/video_vidf_end from badapple_vidf_embed.S), never
 * through fopen/fread. Kept as its own file rather than reusing DOOM's
 * syscalls.c, which references doom_wad_start/doom_wad_end symbols this
 * build doesn't link.
 */

#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include "uart.h"

int _write(int fd, const void *buf, unsigned int count) {
  if (fd == 1 || fd == 2) {
    const char *p = (const char *)buf;
    for (unsigned int i = 0; i < count; i++) uart_putc(p[i]);
    return (int)count;
  }
  errno = EBADF;
  return -1;
}

int _read(int fd, void *buf, unsigned int count) {
  (void)fd; (void)buf; (void)count;
  return 0;
}

int _close(int fd) { (void)fd; return 0; }
long _lseek(int fd, long offset, int whence) { (void)fd; (void)offset; (void)whence; errno = EBADF; return -1; }
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; errno = ENOENT; return -1; }

int _fstat(int fd, struct stat *st) {
  if (fd < 0 || fd > 2) return -1;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

extern char _end[];
#define DRAM_TOP 0x90000000UL
#define STACK_RESERVE (8UL * 1024 * 1024)

void *_sbrk(long incr) {
  static char *heap_ptr = 0;
  if (heap_ptr == 0) heap_ptr = _end;
  char *prev = heap_ptr;
  if ((uintptr_t)(heap_ptr + incr) > (DRAM_TOP - STACK_RESERVE)) {
    errno = ENOMEM;
    return (void *)-1;
  }
  heap_ptr += incr;
  return prev;
}

void _exit(int code) {
  (void)code;
  uart_puts("\r\n[badapple] exited\r\n");
  while (1) { }
}

int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
