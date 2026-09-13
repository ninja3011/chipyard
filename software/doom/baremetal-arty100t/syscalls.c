/*
 * Newlib syscall stubs for bare-metal DOOM on RocketArty100TConfig.
 *
 * Real, elaboration-confirmed facts this file depends on (from
 * fpga/generated-src/.../*.chisel.log and *.dts, generated 2026-08-28):
 *   - DRAM: 0x80000000 - 0x90000000 (256MB, matches the board's DDR3L)
 *   - UART0: 0x10020000, sifive,uart0, clocked at 50MHz (see uart.h)
 *
 * There is no real filesystem here. _open/_read/_lseek/_close special-case
 * any path whose basename ends in ".wad" (case-insensitive) and serve it
 * out of the WAD blob linked in by wad_embed.S. Everything else (savegames,
 * config files, the debug waddump.txt) fails open cleanly, which is exactly
 * what upstream doomgeneric already handles as "no such file."
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "uart.h"

extern char doom_wad_start[];
extern char doom_wad_end[];

#define MAX_OPEN_FILES 8
#define WAD_FD_BASE 100 /* keep well clear of stdin/stdout/stderr (0-2) */

typedef struct {
  int in_use;
  long pos;
} wad_file_t;

static wad_file_t s_wadFiles[MAX_OPEN_FILES];

/* Must match ONLY the actual embedded WAD's name, not any *.wad. DOOM's
 * D_FindWADByName() tries a fixed list of candidate IWAD names in order
 * (doom2.wad, plutonia.wad, tnt.wad, doom.wad, doom1.wad, ..., THEN
 * freedoom1.wad) via M_FileExists() before ever asking for the one we
 * actually have. A loose "any *.wad exists" match would make DOOM believe
 * doom2.wad exists first, latch onto the wrong game mode (commercial/Doom
 * II instead of retail/Freedoom), and then choke on mismatched WAD content
 * served under the wrong identity -- a real bug caught before hardware
 * time, not during it. */
static int path_is_wad(const char *path) {
  const char *base = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  static const char target[] = "freedoom1.wad";
  size_t blen = strlen(base);
  size_t tlen = sizeof(target) - 1;
  if (blen != tlen) return 0;
  for (size_t i = 0; i < tlen; i++) {
    char a = base[i], b = target[i];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (a != b) return 0;
  }
  return 1;
}

int _open(const char *path, int flags, int mode) {
  (void)flags; (void)mode;
  if (!path_is_wad(path)) {
    errno = ENOENT;
    return -1;
  }
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (!s_wadFiles[i].in_use) {
      s_wadFiles[i].in_use = 1;
      s_wadFiles[i].pos = 0;
      return WAD_FD_BASE + i;
    }
  }
  errno = EMFILE;
  return -1;
}

static wad_file_t *wad_file_for_fd(int fd) {
  int idx = fd - WAD_FD_BASE;
  if (idx < 0 || idx >= MAX_OPEN_FILES || !s_wadFiles[idx].in_use) return NULL;
  return &s_wadFiles[idx];
}

int _close(int fd) {
  wad_file_t *f = wad_file_for_fd(fd);
  if (!f) {
    /* stdin/stdout/stderr: nothing to do, not an error. */
    if (fd >= 0 && fd <= 2) return 0;
    errno = EBADF;
    return -1;
  }
  f->in_use = 0;
  return 0;
}

long _lseek(int fd, long offset, int whence) {
  wad_file_t *f = wad_file_for_fd(fd);
  if (!f) { errno = EBADF; return -1; }
  long wad_size = (long)(doom_wad_end - doom_wad_start);
  long newpos;
  switch (whence) {
    case 0 /* SEEK_SET */: newpos = offset; break;
    case 1 /* SEEK_CUR */: newpos = f->pos + offset; break;
    case 2 /* SEEK_END */: newpos = wad_size + offset; break;
    default: errno = EINVAL; return -1;
  }
  if (newpos < 0) { errno = EINVAL; return -1; }
  f->pos = newpos;
  return newpos;
}

int _read(int fd, void *buf, unsigned int count) {
  if (fd == 0) {
    /* stdin: blocking read of keypress bytes off the console UART. */
    unsigned char *p = (unsigned char *)buf;
    unsigned int n = 0;
    while (n < count) {
      int c = uart_getc_nonblock();
      if (c < 0) break;
      p[n++] = (unsigned char)c;
    }
    return (int)n;
  }
  wad_file_t *f = wad_file_for_fd(fd);
  if (!f) { errno = EBADF; return -1; }
  long wad_size = (long)(doom_wad_end - doom_wad_start);
  if (f->pos >= wad_size) return 0;
  long remaining = wad_size - f->pos;
  unsigned int n = (unsigned int)(remaining < (long)count ? remaining : (long)count);
  memcpy(buf, doom_wad_start + f->pos, n);
  f->pos += n;
  return (int)n;
}

int _write(int fd, const void *buf, unsigned int count) {
  if (fd == 1 || fd == 2) {
    const char *p = (const char *)buf;
    for (unsigned int i = 0; i < count; i++) uart_putc(p[i]);
    return (int)count;
  }
  errno = EBADF;
  return -1;
}

int _fstat(int fd, struct stat *st) {
  wad_file_t *f = wad_file_for_fd(fd);
  memset(st, 0, sizeof(*st));
  if (f) {
    st->st_mode = S_IFREG;
    st->st_size = (long)(doom_wad_end - doom_wad_start);
    return 0;
  }
  if (fd >= 0 && fd <= 2) {
    st->st_mode = S_IFCHR;
    return 0;
  }
  errno = EBADF;
  return -1;
}

int _isatty(int fd) {
  return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/* Bump-allocator heap: DOOM's zone allocator (Z_Init) does one big malloc
 * up front, then sub-allocates itself, so we don't need free() to do
 * anything real. Heap grows up from the linker-provided _end towards
 * the stack.
 *
 * HEAP_LIMIT must equal doom_start.S's initial sp (0x87000000) exactly
 * -- not the old assumption of "DRAM's top minus a reserve". Two things
 * changed since that comment was written: (1) DRAM only reliably works
 * up to ~120MB (0x87800000) -- confirmed via a standalone probe payload,
 * not the full 256MB the SoC's memory map claims -- so a ceiling derived
 * from 0x90000000 was already wrong; (2) the stack pointer used to be an
 * unstated assumption ("DRAM's top minus 8MB") but is now a fixed,
 * known address set explicitly in doom_start.S. Deriving the heap
 * ceiling from that same fixed address (instead of from the DRAM size)
 * makes heap-vs-stack collision impossible by construction: the heap
 * physically cannot grow past where the stack begins. */
extern char _end[];
#define HEAP_LIMIT 0x87000000UL

void *_sbrk(long incr) {
  static char *heap_ptr = 0;
  if (heap_ptr == 0) heap_ptr = _end;
  char *prev = heap_ptr;
  if ((uintptr_t)(heap_ptr + incr) > HEAP_LIMIT) {
    errno = ENOMEM;
    return (void *)-1;
  }
  heap_ptr += incr;
  return prev;
}

void _exit(int code) {
  uart_puts("\r\n[doom] exited with code ");
  /* minimal inline itoa, avoids depending on stdio here */
  char buf[12];
  int i = 10;
  buf[11] = 0;
  int neg = code < 0;
  unsigned int v = neg ? -code : code;
  if (v == 0) buf[i--] = '0';
  while (v) { buf[i--] = '0' + (v % 10); v /= 10; }
  if (neg) buf[i--] = '-';
  uart_puts(&buf[i + 1]);
  uart_puts("\r\n");
  while (1) { }
}

int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }

/* No real filesystem to create directories on -- M_MakeDirectory (savegame
 * folder setup) just needs this to not be a fatal link error; savegames
 * degrade to failing at the actual write, which is fine for a first cut. */
int mkdir(const char *path, unsigned int mode) { (void)path; (void)mode; return 0; }
