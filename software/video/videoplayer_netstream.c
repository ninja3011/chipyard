// Plays back a .vidf frame file (see video2frames.py) by streaming frames
// over TCP to a remote viewer, using the exact same wire protocol as
// doomgeneric_netstream.c — the viewer (netstream_viewer.py) needs zero
// changes to display either.
//
// .vidf format:
//   char[4]   magic = "VIDF"
//   uint32_t  width, height, num_frames, fps   (little-endian)
//   then num_frames * ceil(width*height/8) bytes of packed 1-bit frames,
//   row-major, MSB-first. bit=1 -> white, bit=0 -> black.
//
// Wire protocol out (identical to doomgeneric_netstream.c):
//   char[4]  magic = "DGFR"
//   uint32_t width, height
//   uint8_t  pixels[width*height*4]   (RGBA, unpacked from the 1-bit source)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define NETSTREAM_PORT 5678

static int s_listenFd = -1;
static int s_clientFd = -1;

static void acceptClientIfNeeded(void) {
  if (s_clientFd >= 0) return;
  struct sockaddr_in clientAddr;
  socklen_t clientLen = sizeof(clientAddr);
  printf("videoplayer_netstream: waiting for viewer to connect on port %d...\n",
         NETSTREAM_PORT);
  int fd = accept(s_listenFd, (struct sockaddr *)&clientAddr, &clientLen);
  if (fd < 0) {
    perror("videoplayer_netstream: accept");
    return;
  }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  s_clientFd = fd;
  printf("videoplayer_netstream: viewer connected from %s\n",
         inet_ntoa(clientAddr.sin_addr));
}

static int sendFrame(uint32_t w, uint32_t h, const uint8_t *rgba) {
  struct {
    char magic[4];
    uint32_t width;
    uint32_t height;
  } header = {{'D', 'G', 'F', 'R'}, w, h};

  if (send(s_clientFd, &header, sizeof(header), MSG_NOSIGNAL) != (ssize_t)sizeof(header))
    return -1;

  size_t frameBytes = (size_t)w * h * 4;
  size_t off = 0;
  while (off < frameBytes) {
    ssize_t n = send(s_clientFd, rgba + off, frameBytes - off, MSG_NOSIGNAL);
    if (n <= 0) return -1;
    off += (size_t)n;
  }
  return 0;
}

// Unpacks one row-major 1-bit frame (MSB-first) into an RGBA buffer.
static void unpack1bit(const uint8_t *packed, uint32_t w, uint32_t h, uint8_t *rgbaOut) {
  size_t rowBytes = (w + 7) / 8;
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t *row = packed + y * rowBytes;
    for (uint32_t x = 0; x < w; x++) {
      int bit = (row[x / 8] >> (7 - (x % 8))) & 1;
      uint8_t v = bit ? 0xFF : 0x00;
      uint8_t *px = rgbaOut + (size_t)(y * w + x) * 4;
      px[0] = v; // B
      px[1] = v; // G
      px[2] = v; // R
      px[3] = 0; // unused, matches doomgeneric's layout
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.vidf> [loop:0|1]\n", argv[0]);
    return 1;
  }
  int loop = (argc >= 3) ? atoi(argv[2]) : 1;

  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror("fopen");
    return 1;
  }

  char magic[4];
  uint32_t width, height, numFrames, fps;
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "VIDF", 4) != 0) {
    fprintf(stderr, "not a .vidf file\n");
    return 1;
  }
  fread(&width, 4, 1, f);
  fread(&height, 4, 1, f);
  fread(&numFrames, 4, 1, f);
  fread(&fps, 4, 1, f);
  printf("videoplayer_netstream: %ux%u, %u frames @ %ufps\n",
         width, height, numFrames, fps);

  long headerEnd = ftell(f);
  size_t rowBytes = (width + 7) / 8;
  size_t packedFrameBytes = rowBytes * height;
  size_t rgbaFrameBytes = (size_t)width * height * 4;

  uint8_t *packedBuf = malloc(packedFrameBytes);
  uint8_t *rgbaBuf = malloc(rgbaFrameBytes);
  if (!packedBuf || !rgbaBuf) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }

  s_listenFd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(NETSTREAM_PORT);
  if (bind(s_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  listen(s_listenFd, 1);
  acceptClientIfNeeded();

  long frameIntervalNs = 1000000000L / (fps ? fps : 30);

  do {
    fseek(f, headerEnd, SEEK_SET);
    for (uint32_t i = 0; i < numFrames; i++) {
      acceptClientIfNeeded();
      if (fread(packedBuf, 1, packedFrameBytes, f) != packedFrameBytes) {
        fprintf(stderr, "short read at frame %u\n", i);
        break;
      }
      unpack1bit(packedBuf, width, height, rgbaBuf);

      if (s_clientFd >= 0) {
        if (sendFrame(width, height, rgbaBuf) < 0) {
          close(s_clientFd);
          s_clientFd = -1;
        }
      }

      struct timespec ts = {frameIntervalNs / 1000000000L, frameIntervalNs % 1000000000L};
      nanosleep(&ts, NULL);
    }
  } while (loop);

  free(packedBuf);
  free(rgbaBuf);
  fclose(f);
  return 0;
}
