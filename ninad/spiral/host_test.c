#include <stdio.h>
static void print_str(const char *s){ fputs(s, stdout); }
static void uart_putc(char c){ putchar((unsigned char)c); }

typedef int fp_t; // Q8.8 fixed point

#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)

// sin(0..90 degrees) in Q8.8, i.e. round(sin(d) * 256). Generated with:
//   python3 -c "import math; print([round(math.sin(math.radians(d))*256) for d in range(91)])"
// -- not hand-typed, same discipline as the DMI ROM generator: a table this
// size is exactly the kind of thing that's easy to mistranscribe by hand.
static const short sin_table[91] = {
    0, 4, 9, 13, 18, 22, 27, 31, 36, 40,
    44, 49, 53, 58, 62, 66, 71, 75, 79, 83,
    88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
    128, 132, 136, 139, 143, 147, 150, 154, 158, 161,
    165, 168, 171, 175, 178, 181, 184, 187, 190, 193,
    196, 199, 202, 204, 207, 210, 212, 215, 217, 219,
    222, 224, 226, 228, 230, 232, 234, 236, 237, 239,
    241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
    252, 253, 254, 254, 255, 255, 255, 256, 256, 256,
    256
};

// Full-circle sin via quadrant folding of the 0-90 table; cos is just sin
// shifted 90 degrees.
static fp_t sin_deg(int d) {
    d = ((d % 360) + 360) % 360;
    if (d <= 90)  return sin_table[d];
    if (d <= 180) return sin_table[180 - d];
    if (d <= 270) return -sin_table[d - 180];
    return -sin_table[360 - d];
}

static fp_t cos_deg(int d) {
    return sin_deg(d + 90);
}

#define WIDTH   78
#define HEIGHT  24
static char canvas[HEIGHT][WIDTH];

static void plot(int x, int y, char c) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) canvas[y][x] = c;
}

int main(void) {
    print_str("Archimedean Spiral Web (TinyRocket, Q8.8 fixed point)\r\n");
    print_str("Spinning a web...\r\n\r\n");

    int cx = WIDTH / 2;
    int cy = HEIGHT / 2;

    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            canvas[y][x] = ' ';

    // r(theta) = b * theta, in Q8.8 world units, theta in integer degrees.
    // b chosen so r reaches ~20.0 world units (Q8.8: 20*256=5120) after
    // TURNS full revolutions -- 20 is the tighter of the two screen limits
    // once the y-axis is halved below to correct for terminal characters
    // being roughly twice as tall as they are wide.
    #define TURNS    4
    #define THETA_MAX (TURNS * 360)
    #define MAXR_Q88  5120
    const fp_t b = (MAXR_Q88 * FP_ONE) / THETA_MAX; // Q8.8 slope

    // Eight spokes first (the web's structural threads), then the spiral is
    // drawn on top, same as it physically sits in a real orb web.
    static const int spoke_deg[8] = {0, 45, 90, 135, 180, 225, 270, 315};
    static const char spoke_char[8] = {'-', '\\', '|', '/', '-', '\\', '|', '/'};
    for (int s = 0; s < 8; s++) {
        fp_t cv = cos_deg(spoke_deg[s]);
        fp_t sv = sin_deg(spoke_deg[s]);
        for (int rr = 0; rr <= 20; rr++) {
            fp_t r = rr * FP_ONE;
            // r and cv/sv are both Q8.8, so their product is Q16.16 -- shift
            // right by 2*FP_SHIFT (not FP_SHIFT) to collapse all the way
            // down to a plain integer pixel offset.
            int dx = (r * cv) >> (2 * FP_SHIFT);
            int dy = ((r * sv) >> (2 * FP_SHIFT)) / 2; // aspect fix
            plot(cx + dx, cy + dy, spoke_char[s]);
        }
    }

    int spider_x = cx, spider_y = cy;
    for (int theta = 0; theta <= THETA_MAX; theta += 3) {
        fp_t r = (b * theta) >> FP_SHIFT; // Q8.8 world-unit radius
        fp_t cv = cos_deg(theta);
        fp_t sv = sin_deg(theta);
        int dx = (r * cv) >> (2 * FP_SHIFT);
        int dy = ((r * sv) >> (2 * FP_SHIFT)) / 2; // aspect fix
        plot(cx + dx, cy + dy, '.');
        if (theta == THETA_MAX - 90) { spider_x = cx + dx; spider_y = cy + dy; }
    }

    plot(cx, cy, '+');       // the hub
    plot(spider_x, spider_y, '@'); // the spider, out on its web

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) uart_putc(canvas[y][x]);
        uart_putc('\r');
        uart_putc('\n');
    }

    print_str("\r\n");
    print_str("  /\\oo/\\  \r\n");
    print_str(" /      \\ \r\n");
    print_str("=( o  o )=\r\n");
    print_str(" \\  ||  / \r\n");
    print_str("  \\_||_/  \r\n");
    print_str("\r\n");
    print_str("Your friendly neighborhood spiral-slinger.\r\n");
    print_str("With great radius comes great responsibility.\r\n");
    print_str("Done.\r\n");
    return 0;
}
