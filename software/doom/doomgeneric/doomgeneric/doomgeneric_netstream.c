// doomgeneric backend that streams frames over TCP to a remote viewer and
// receives keyboard input the same way. Built for running DOOM on a headless
// target (no framebuffer device) reachable only over the network — e.g. a
// FireSim-simulated RISC-V Linux target with no display hardware, reached
// through the FireSim NIC bridge (IceNIC) from the run-farm host.
//
// Wire protocol (server = target, client = viewer):
//   Server -> Client, once per frame:
//     char[4]  magic = "DGFR"
//     uint32_t width      (little-endian)
//     uint32_t height     (little-endian)
//     uint8_t  pixels[width*height*4]   (RGBA, matches DG_ScreenBuffer layout)
//   Client -> Server, one per key event:
//     uint8_t  pressed (0 or 1)
//     uint8_t  doom_keycode

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define DOOM_NET_DEFAULT_PORT 5678
#define KEYQUEUE_SIZE 64

static int s_listenFd = -1;
static int s_clientFd = -1;
static pthread_t s_inputThread;
static pthread_mutex_t s_keyLock = PTHREAD_MUTEX_INITIALIZER;

static unsigned short s_KeyQueue[KEYQUEUE_SIZE]; // (pressed<<8)|key
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static uint32_t s_startTicks = 0;

static void pushKey(int pressed, unsigned char key) {
  pthread_mutex_lock(&s_keyLock);
  s_KeyQueue[s_KeyQueueWriteIndex] = ((pressed ? 1 : 0) << 8) | key;
  s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
  pthread_mutex_unlock(&s_keyLock);
}

static void *inputThreadMain(void *arg) {
  (void)arg;
  unsigned char buf[2];
  for (;;) {
    if (s_clientFd < 0) {
      usleep(50 * 1000);
      continue;
    }
    ssize_t n = recv(s_clientFd, buf, 2, MSG_WAITALL);
    if (n != 2) {
      // client dropped; close and wait for a fresh accept from DG_DrawFrame
      close(s_clientFd);
      s_clientFd = -1;
      continue;
    }
    pushKey(buf[0] != 0, buf[1]);
  }
  return NULL;
}

static void acceptClientIfNeeded(void) {
  if (s_clientFd >= 0) return;

  struct sockaddr_in clientAddr;
  socklen_t clientLen = sizeof(clientAddr);
  printf("doomgeneric_netstream: waiting for viewer to connect on port %d...\n",
         DOOM_NET_DEFAULT_PORT);
  int fd = accept(s_listenFd, (struct sockaddr *)&clientAddr, &clientLen);
  if (fd < 0) {
    perror("doomgeneric_netstream: accept");
    return;
  }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  s_clientFd = fd;
  printf("doomgeneric_netstream: viewer connected from %s\n",
         inet_ntoa(clientAddr.sin_addr));
}

void DG_Init(void) {
  s_listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (s_listenFd < 0) {
    perror("doomgeneric_netstream: socket");
    exit(1);
  }
  int one = 1;
  setsockopt(s_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(DOOM_NET_DEFAULT_PORT);

  if (bind(s_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("doomgeneric_netstream: bind");
    exit(1);
  }
  if (listen(s_listenFd, 1) < 0) {
    perror("doomgeneric_netstream: listen");
    exit(1);
  }

  pthread_create(&s_inputThread, NULL, inputThreadMain, NULL);

  // Block startup until the viewer is actually attached — no point
  // simulating gameplay nobody is watching yet.
  acceptClientIfNeeded();

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  s_startTicks = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void DG_DrawFrame(void) {
  acceptClientIfNeeded();
  if (s_clientFd < 0) return;

  static const char magic[4] = {'D', 'G', 'F', 'R'};
  uint32_t w = (uint32_t)DOOMGENERIC_RESX;
  uint32_t h = (uint32_t)DOOMGENERIC_RESY;

  struct iovec_header {
    char magic[4];
    uint32_t width;
    uint32_t height;
  } header;
  memcpy(header.magic, magic, 4);
  header.width = w;
  header.height = h;

  ssize_t sent = send(s_clientFd, &header, sizeof(header), MSG_NOSIGNAL);
  if (sent != (ssize_t)sizeof(header)) {
    close(s_clientFd);
    s_clientFd = -1;
    return;
  }

  size_t frameBytes = (size_t)w * h * sizeof(pixel_t);
  size_t off = 0;
  const char *p = (const char *)DG_ScreenBuffer;
  while (off < frameBytes) {
    ssize_t n = send(s_clientFd, p + off, frameBytes - off, MSG_NOSIGNAL);
    if (n <= 0) {
      close(s_clientFd);
      s_clientFd = -1;
      return;
    }
    off += (size_t)n;
  }
}

void DG_SleepMs(uint32_t ms) { usleep(ms * 1000); }

uint32_t DG_GetTicksMs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint32_t now = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
  return now - s_startTicks;
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
  pthread_mutex_lock(&s_keyLock);
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
    pthread_mutex_unlock(&s_keyLock);
    return 0;
  }
  unsigned short entry = s_KeyQueue[s_KeyQueueReadIndex];
  s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
  pthread_mutex_unlock(&s_keyLock);

  *pressed = (entry >> 8) & 1;
  *doomKey = entry & 0xff;
  return 1;
}

void DG_SetWindowTitle(const char *title) {
  (void)title; // no window to title on a headless target
}

int main(int argc, char **argv) {
  doomgeneric_Create(argc, argv);
  while (1) {
    doomgeneric_Tick();
  }
  return 0;
}
