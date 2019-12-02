/*
 * newtest.c
 *
 * Copyright (c) 2014 Jeremy Garff <jer @ jers.net>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 *     1.  Redistributions of source code must retain the above copyright notice, this list of
 *         conditions and the following disclaimer.
 *     2.  Redistributions in binary form must reproduce the above copyright notice, this list
 *         of conditions and the following disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *     3.  Neither the name of the owner nor the names of its contributors may be used to endorse
 *         or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


static char VERSION[] = "XX.YY.ZZ";

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <getopt.h>


#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"
#include "version.h"

#include "ws2811.h"


#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))

// defaults for cmdline options
#define TARGET_FREQ             WS2811_TARGET_FREQ
#define GPIO_PIN                18
#define DMA                     10
//#define STRIP_TYPE            WS2811_STRIP_RGB		// WS2812/SK6812RGB integrated chip+leds
#define STRIP_TYPE              WS2811_STRIP_GBR		// WS2812/SK6812RGB integrated chip+leds
//#define STRIP_TYPE            SK6812_STRIP_RGBW		// SK6812RGBW (NOT SK6812RGB)

#define WIDTH                   8
#define HEIGHT                  8
#define LED_COUNT               200

int width = WIDTH;
int height = HEIGHT;
int led_count = LED_COUNT;

int clear_on_exit = 0;

ws2811_t ledstring =
{
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel =
    {
        [0] =
        {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255,
            .strip_type = STRIP_TYPE,
        },
        [1] =
        {
            .gpionum = 0,
            .count = 0,
            .invert = 0,
            .brightness = 0,
        },
    },
};

ws2811_led_t *matrix;

static uint8_t running = 1;

void matrix_render(void)
{
    int x, y;

    for (x = 0; x < width; x++)
    {
        for (y = 0; y < height; y++)
        {
            ledstring.channel[0].leds[(y * width) + x] = matrix[y * width + x];
        }
    }
}

void matrix_raise(void)
{
    int x, y;

    for (y = 0; y < (height - 1); y++)
    {
        for (x = 0; x < width; x++)
        {
            // This is for the 8x8 Pimoroni Unicorn-HAT where the LEDS in subsequent
            // rows are arranged in opposite directions
            matrix[y * width + x] = matrix[(y + 1)*width + width - x - 1];
        }
    }
}

void matrix_clear(void)
{
    int x, y;

    for (y = 0; y < (height ); y++)
    {
        for (x = 0; x < width; x++)
        {
            matrix[y * width + x] = 0;
        }
    }
}

int dotspos[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
ws2811_led_t dotcolors[] =
{
    0x00200000,  // red
    0x00201000,  // orange
    0x00202000,  // yellow
    0x00002000,  // green
    0x00002020,  // lightblue
    0x00000020,  // blue
    0x00100010,  // purple
    0x00200010,  // pink
};

ws2811_led_t dotcolors_rgbw[] =
{
    0x00200000,  // red
    0x10200000,  // red + W
    0x00002000,  // green
    0x10002000,  // green + W
    0x00000020,  // blue
    0x10000020,  // blue + W
    0x00101010,  // white
    0x10101010,  // white + W

};

void matrix_bottom(void)
{
    int i;

    for (i = 0; i < (int)(ARRAY_SIZE(dotspos)); i++)
    {
        dotspos[i]++;
        if (dotspos[i] > (width - 1))
        {
            dotspos[i] = 0;
        }

        if (ledstring.channel[0].strip_type == SK6812_STRIP_RGBW) {
            matrix[dotspos[i] + (height - 1) * width] = dotcolors_rgbw[i];
        } else {
            matrix[dotspos[i] + (height - 1) * width] = dotcolors[i];
        }
    }
}

static void ctrl_c_handler(int signum)
{
    (void)(signum);
    running = 0;
}

static void setup_handlers(void)
{
    struct sigaction sa =
    {
        .sa_handler = ctrl_c_handler,
    };

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static const char *mode;
static int fps = 30;

void parseargs(int argc, char **argv, ws2811_t *ws2811)
{
    int index;
    int c;

    static struct option longopts[] =
    {
        {"help", no_argument, 0, 'h'},
        {"dma", required_argument, 0, 'd'},
        {"gpio", required_argument, 0, 'g'},
        {"invert", no_argument, 0, 'i'},
        {"clear", no_argument, 0, 'c'},
        {"strip", required_argument, 0, 's'},
        {"height", required_argument, 0, 'y'},
        {"width", required_argument, 0, 'x'},
        {"version", no_argument, 0, 'v'},
                {"mode", required_argument, 0, 'm'},
                {"rate", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    while (1)
    {

        index = 0;
        c = getopt_long(argc, argv, "cd:g:his:vx:y:m:r:", longopts, &index);

        if (c == -1)
            break;

        switch (c)
        {
        case 0:
            /* handle flag options (array's 3rd field non-0) */
            break;

        case 'h':
            fprintf(stderr, "%s version %s\n", argv[0], VERSION);
            fprintf(stderr, "Usage: %s \n"
                "-h (--help)    - this information\n"
                "-s (--strip)   - strip type - rgb, grb, gbr, rgbw\n"
                "-x (--width)   - matrix width (default 8)\n"
                "-y (--height)  - matrix height (default 8)\n"
                "-d (--dma)     - dma channel to use (default 10)\n"
                "-g (--gpio)    - GPIO to use\n"
                "                 If omitted, default is 18 (PWM0)\n"
                "-i (--invert)  - invert pin output (pulse LOW)\n"
                "-c (--clear)   - clear matrix on exit.\n"
                "-m (--mode)    - animate chase, fade, rainbow, blink, or color\n"
                "-r (--rate)    - update animation at N fps\n"
                "-v (--version) - version information\n"
                , argv[0]);
            exit(-1);

        case 'D':
            break;

        case 'g':
            if (optarg) {
                int gpio = atoi(optarg);
/*
    PWM0, which can be set to use GPIOs 12, 18, 40, and 52.
    Only 12 (pin 32) and 18 (pin 12) are available on the B+/2B/3B
    PWM1 which can be set to use GPIOs 13, 19, 41, 45 and 53.
    Only 13 is available on the B+/2B/PiZero/3B, on pin 33
    PCM_DOUT, which can be set to use GPIOs 21 and 31.
    Only 21 is available on the B+/2B/PiZero/3B, on pin 40.
    SPI0-MOSI is available on GPIOs 10 and 38.
    Only GPIO 10 is available on all models.

    The library checks if the specified gpio is available
    on the specific model (from model B rev 1 till 3B)

*/
                ws2811->channel[0].gpionum = gpio;
            }
            break;

        case 'i':
            ws2811->channel[0].invert=1;
            break;

        case 'c':
            clear_on_exit=1;
            break;

        case 'd':
            if (optarg) {
                int dma = atoi(optarg);
                if (dma < 14) {
                    ws2811->dmanum = dma;
                } else {
                    printf ("invalid dma %d\n", dma);
                    exit (-1);
                }
            }
            break;

                case 'm':
                        mode = optarg;
                        break;

                case 'r':
                        fps = atoi(optarg);
                        break;

        case 'y':
            if (optarg) {
                height = atoi(optarg);
                if (height > 0) {
                    ws2811->channel[0].count = height * width;
                } else {
                    printf ("invalid height %d\n", height);
                    exit (-1);
                }
            }
            break;

        case 'x':
            if (optarg) {
                width = atoi(optarg);
                if (width > 0) {
                    ws2811->channel[0].count = height * width;
                } else {
                    printf ("invalid width %d\n", width);
                    exit (-1);
                }
            }
            break;

        case 's':
            if (optarg) {
                if (!strncasecmp("rgb", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_RGB;
                }
                else if (!strncasecmp("rbg", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_RBG;
                }
                else if (!strncasecmp("grb", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_GRB;
                }
                else if (!strncasecmp("gbr", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_GBR;
                }
                else if (!strncasecmp("brg", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_BRG;
                }
                else if (!strncasecmp("bgr", optarg, 4)) {
                    ws2811->channel[0].strip_type = WS2811_STRIP_BGR;
                }
                else if (!strncasecmp("rgbw", optarg, 4)) {
                    ws2811->channel[0].strip_type = SK6812_STRIP_RGBW;
                }
                else if (!strncasecmp("grbw", optarg, 4)) {
                    ws2811->channel[0].strip_type = SK6812_STRIP_GRBW;
                }
                else {
                    printf ("invalid strip %s\n", optarg);
                    exit (-1);
                }
            }
            break;

        case 'v':
            fprintf(stderr, "%s version %s\n", argv[0], VERSION);
            exit(-1);

        case '?':
            /* getopt_long already reported error? */
            exit(-1);

        default:
            exit(-1);
        }
    }
}

#define blue    0x00010000
#define red     0x00000100
#define green   0x00000001
#define cyan    (blue + green)
#define yellow  (green + red)
#define magenta (blue + red)
#define white   (blue + red + green)
#define orange  (2*red + green)
int sparkle_colors[] = { blue, red, green, cyan, yellow, magenta, white };

#define FADERAND 12

void fade(int frame)
{
    int i;
    int pos = frame % 0x80;
    int color = (frame / 0x80) % ARRAY_SIZE(sparkle_colors);
    for (i=0; i<LED_COUNT; i++) {
        if (pos <= 0x40) {
            ledstring.channel[0].leds[i] = sparkle_colors[color] * (pos + (rand() % FADERAND));
        }
        else {
            ledstring.channel[0].leds[i] = sparkle_colors[color] * (0x80-pos + (rand() % FADERAND));
        }
    }
}

void chase(int frame, int mod)
{
    static int color[LED_COUNT+1];
    int i;
    if (frame % 2 == 0) {
        for (i=LED_COUNT-1; i>=1; i--) {
            color[i] = color[i-1];
        }
        if (frame % mod == 0) {
            color[0] = sparkle_colors[rand() % ARRAY_SIZE(sparkle_colors)];
        }
        else {
            color[0] = 0;
        }
        for (i=0; i<LED_COUNT; i++) {
            ledstring.channel[0].leds[i] = color[i] * 0x80;
        }
    }
    else {
        for (i=1; i<LED_COUNT; i++) {
            ledstring.channel[0].leds[i] = (color[i] + color[i-1]) * 0x20;
        }
    }
}

void sparkle()
{
    static int color[LED_COUNT];
    static int intensity[LED_COUNT];
    int i;
    for (i=0; i<2; i++) {
        int r = rand() % LED_COUNT;
        color[r] = sparkle_colors[rand() % ARRAY_SIZE(sparkle_colors)];
        intensity[r] = 0xFF;
    }
    for (i=0; i<LED_COUNT; i++) {
        intensity[i] = intensity[i] * 0.87;
        ledstring.channel[0].leds[i] = color[i] * intensity[i];
    }
}

//int rainbow_colors[] = { red, yellow, green, blue, magenta, red };
//int rainbow_colors[] = { cyan, white, blue, magenta, green, cyan };
//int rainbow_colors[] = { red, orange, yellow, magenta, white, red };
int rainbow_colors[] = { red, orange, yellow, green, blue, magenta, red };
void rainbow(int frame)
{
    int i;
    for (i=0; i<LED_COUNT; i++) {
        int mi = (i + frame/3) % LED_COUNT;
        float rpos = mi * (float)(ARRAY_SIZE(rainbow_colors)-1) / (LED_COUNT-1);
        int a = (int)rpos;
        int b = a + 1;
        float b_pct = rpos - a;
        float a_pct = 1 - b_pct;
        ledstring.channel[0].leds[i] =
            rainbow_colors[a] * (int)(0x20*a_pct) +
            rainbow_colors[b] * (int)(0x20*b_pct);
    }
}

unsigned int scale(unsigned int color, unsigned int pct)
{
    return (((color & 0xff0000) * pct / 100) & 0xff0000) +
           (((color & 0x00ff00) * pct / 100) & 0x00ff00) +
           (((color & 0x0000ff) * pct / 100) & 0x0000ff);
}

unsigned int mix(unsigned int color1, unsigned int color2, unsigned int pct)
{
    return scale(color1, 100-pct) + scale(color2, pct);
}

void statefair(int frame, unsigned long color1, unsigned long color2, int reverse)
{
    int i;
    for (i=0; i<LED_COUNT; i++) {
        int mframe = (LED_COUNT + frame - i) % 64;
        int color = (mframe < 32) ? color1 : color2;
        int idx = reverse ? (LED_COUNT-i-1) : i;
        ledstring.channel[0].leds[idx] = scale(color, (32-mframe%32)*100/32);
    }
}

void halloween(int frame)
{
    statefair(frame, 0xff8000, 0x00ff40, 0);
}

void christmas(int frame)
{
    statefair(frame, 0x00ff00, 0x0000ff, 1);
}

void candycane(int frame)
{
    statefair(frame, 0x008000, 0x88ffff, 1);
}

void pure_random(int frame)
{
    int i;
    for (i=0; i<LED_COUNT; i++) {
        ledstring.channel[0].leds[i] =
           red * (rand() % 20) + 
           green * (rand() % 20) + 
           blue * (rand() % 20);
    }
}

struct { const char *name; unsigned long color; } named_colors[] = {
    { "red",      0x500202 },
    { "green",    0x208010 },
    { "blue",     0x103080 },
    { "pink",     0x801010 },
    { "purple",   0x400080 },
    { "orange",   0x802000 },
    { "yellow",   0x905000 },
    { "teal",     0x008030 },
    { "gold",     0x805010 },
    { "cool",     0x505080 },
    { "white",    0x808040 },
};

void blank()
{
    int i;
    for (i=0; i<LED_COUNT; i++)
        ledstring.channel[0].leds[i] = 0;
}

unsigned long color_for_name(const char *name)
{
    unsigned int i;
    unsigned long color = 0;
    for (i=0; i<ARRAY_SIZE(named_colors); i++) {
        if (!strcmp(name, named_colors[i].name)) {
            color = named_colors[i].color;
            break;
        }
    }
    if (!color) color = strtoul(name, 0, 16);

    color = ((color & 0x00FF0000) >> 8) +
        ((color & 0x0000FF00) >> 8) +
        ((color & 0x000000FF) << 16);

    return color;
}

void plain_color(const char *name)
{
    unsigned long color = color_for_name(name);
    int i;
    for (i=0; i<LED_COUNT; i++) {
        ledstring.channel[0].leds[i] = color;
    }
}

void pattern(const char *names)
{
    char *dup = strdup(names);
    char *p;
    unsigned long colors[20];
    int i, ncolors=0;
    for (p=strtok(dup, ","); p && ncolors < ARRAY_SIZE(colors); p=strtok(NULL, ",")) {
        colors[ncolors++] = color_for_name(p);
    }

    if (ncolors == 0) colors[ncolors++] = 0;

    for (i=0; i<LED_COUNT; i++) {
        ledstring.channel[0].leds[i] = colors[i % ncolors];
    }
}

void lightning_helper(int frame, int colors)
{
    #define BOLT_COUNT 40
    static struct { int pos, age; unsigned int color; } bolts[BOLT_COUNT];
    // (1..20).map{|x| 12700/(x**1.5)}.map(&:to_i)
    static int decay_table[] = { 12700, 2444, 1135, 12700, 4490, 2444, 1587, 1135, 864, 685, 561, 470, 401, 348, 305, 270, 242, 218, 198, 181, 166, 153, 141 };
    static int color_table[] = { orange, magenta, red };
    static int next_bolt = 0;
    int i;

    blank();

    if (rand() % 8 == 0) {
        bolts[next_bolt].pos = rand() % LED_COUNT;
        bolts[next_bolt].age = 1;
        bolts[next_bolt].color = colors ? color_table[(rand() % ARRAY_SIZE(color_table))] : white;
        next_bolt = (next_bolt + 1) % BOLT_COUNT;
    }

    for (i=0; i<BOLT_COUNT; i++) {
        if (bolts[i].age >= 23) bolts[i].pos = -1;
        if (bolts[i].pos == -1 || bolts[i].age == 0) continue;
        ledstring.channel[0].leds[bolts[i].pos] = scale(bolts[i].color, decay_table[bolts[i].age-1]);
        bolts[i].age++;
    }
}

void lightning(int frame) { lightning_helper(frame, 0); }
void fireflies(int frame) { lightning_helper(frame, 1); }

#define SCOOT_COUNT 10
void scoot(int frame)
{
    static int bolts[SCOOT_COUNT];
    static int next_bolt = 0;
    int i;

    blank();

    if (rand() % 25 == 0) {
        bolts[next_bolt] = (next_bolt % 2) ? 3 : (LED_COUNT-3);
        next_bolt = (next_bolt + 1) % SCOOT_COUNT;
    }

    for (i=0; i<SCOOT_COUNT; i++) {
        if (bolts[i] < 0 || bolts[i] >= LED_COUNT) continue;
        int color = (i % 2) ? 0x00ff00 : 0x0000ff;
        ledstring.channel[0].leds[bolts[i]] = color;
        int delta = 1;//(rand() % 5) - 1;
        if (i % 2 == 0) delta = -delta;
        bolts[i] += delta;
    }
}

void interactive()
{
    char str[40];
    if (!fgets(str, sizeof(str), stdin)) {
        running = 0;
        return;
    }

    if (*str == 'c') {
        blank();
    }
    else {
        int pos = atoi(str);
        ledstring.channel[0].leds[pos] = 32*white;
    }
}

static struct { int pos, delta, len; } edge[] = {
// window edges
#if 0
    { 4, 1, 25 },
    { 61, -1, 23 },
    { 105, 1, 25 },
    { 164, -1, 24 }
#endif

// tree is all one edge
    { 0, 1, LED_COUNT-1 },
};

static int rings[] = {
    8, 42, 70, 97, 116, 135, 151, 168, 177, 185, 190, 196, 199, LED_COUNT
};

static struct { int pos, delta, len; } arc[] = {
    { 4, 1, 58 },
    { 164, -1, 60 }
};

void fire(int frame)
{
    int i, j;
    static int frac = 80;
    for (i=0; i<ARRAY_SIZE(edge); i++) {
        int limit = edge[i].len * frac / 100;

        // paint fire colors
        for (j=0; j<limit; j++) {
            int pos = edge[i].pos + j*edge[i].delta;
            int color = mix(25*red, 25*orange, 100*j/limit);
            ledstring.channel[0].leds[pos] = color;
        }

        // paint the rest black
        for (j=limit; j<edge[i].len+1; j++) {
            int pos = edge[i].pos + j*edge[i].delta;
            ledstring.channel[0].leds[pos] = 0;
        }

        // update for next time
        frac += (rand() % 41) - 10 - frac/5;
        if (frac < 0) frac = 0;
        if (frac > 100) frac = 100;
    }
}

void ringfire(int frame)
{
    int i, j;
    static int frac = 80;

    int limit = (ARRAY_SIZE(rings)-1) * frac / 100;
    // paint the fire colors
    for (i=0; i<limit; i++) {
        int color = mix(25*red, 25*orange, 100*i/limit);
        for (j=rings[i]; j<rings[i+1]; j++) {
            ledstring.channel[0].leds[j] = color;
        }
    }

    // paint the rest black
    for (j=rings[limit]; j<LED_COUNT; j++) {
        ledstring.channel[0].leds[j] = 0;
    }

    // update for next time
    frac += (rand() % 41) - 10 - frac/5;
    if (frac < 0) frac = 0;
    if (frac > 100) frac = 100;
}

void parabolic(int frame)
{
    blank();
    int i;
    for (i=0; i<ARRAY_SIZE(arc); i++) {
        float t = (frame % 31)/30.0;
        float v = 2;
        float a = -4;
        float pos;
        pos = v*t + a*t*t/2;
        if (t >= 0.5) pos = 1-pos;
        if ((frame/31)%2) pos = 1-pos;  // alternate between forward and reverse
        int ipos = arc[i].pos + arc[i].delta*pos*arc[i].len;
        int color = (i ^ ((frame/31) & 1)) ? 0x00ff00 : 0x0000ff;
        ledstring.channel[0].leds[ipos] = color;
        if (ipos >= 1) ledstring.channel[0].leds[ipos-1] = scale(color, 25);
        if (ipos >= 2) ledstring.channel[0].leds[ipos-2] = scale(color, 10);
        if (ipos <  LED_COUNT-1) ledstring.channel[0].leds[ipos+1] = scale(color, 25);
        if (ipos <  LED_COUNT-2) ledstring.channel[0].leds[ipos+2] = scale(color, 10);
    }
}

void precip(int frame, int *arr, int arrsize)
{
    int i, j;
    blank();
    for (i=0; i<ARRAY_SIZE(edge); i++) {
        for (j=0; j<edge[i].len; j++) {
            int pos = edge[i].pos + j*edge[i].delta;
            int color = arr[(frame + j) % arrsize];
            ledstring.channel[0].leds[pos] = color;
        }
    }
}

void rain(int frame)
{
    static int rain_colors[] = { 0x300030, 0x800080, 0x600060, 0x400048, 0x300030, 0x200120, 0x100310, 0x040404, 0 };
    precip(frame, rain_colors, ARRAY_SIZE(rain_colors));
}

void snow(int frame)
{
    frame /= 4;
    static int snow_colors[] = { 0x040404, 0x808080, 0x040404, 0x040404, 0x040404, 0x202020, 0x040404, 0x040404, 0 };
    precip(frame/4, snow_colors, ARRAY_SIZE(snow_colors));
}

const char *blink_colors[] = { "red", "yellow", "green", "blue", "purple", "pink" };
void blink(int frame)
{
    if (frame % 2 == 0) {
        blank();
    }
    else {
        plain_color(blink_colors[(frame/2) % ARRAY_SIZE(blink_colors)]);
    }
}

void auto_sequence(int frame)
{
    void (*sequence[])(int) = { parabolic, snow, fire, christmas };
    int seqpos = (frame/450) % ARRAY_SIZE(sequence);
    sequence[seqpos](frame);
}

int main(int argc, char *argv[])
{
    ws2811_return_t ret;

    sprintf(VERSION, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    parseargs(argc, argv, &ledstring);

    matrix = malloc(sizeof(ws2811_led_t) * width * height);

    setup_handlers();

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }

    int frame = 0;
    srand(time(0));

    while (running)
    {
        if (mode && !strcmp(mode, "auto"))
            auto_sequence(frame++);
        else if (mode && !strcmp(mode, "chase"))
            chase(frame++, 12);
        else if (mode && !strcmp(mode, "fade"))
            fade(frame++);
        else if (mode && !strcmp(mode, "rainbow"))
            rainbow(frame++);
        else if (mode && !strcmp(mode, "blink"))
            blink(frame++);
        else if (mode && !strcmp(mode, "random"))
            pure_random(frame++);
        else if (mode && !strcmp(mode, "parabolic"))
            parabolic(frame++);
        else if (mode && !strcmp(mode, "snow"))
            snow(frame++);
        else if (mode && !strcmp(mode, "rain"))
            rain(frame++);
        else if (mode && !strcmp(mode, "lightning"))
            lightning(frame++);
        else if (mode && !strcmp(mode, "fireflies"))
            fireflies(frame++);
        else if (mode && !strcmp(mode, "halloween"))
            halloween(frame++);
        else if (mode && !strcmp(mode, "christmas"))
            christmas(frame++);
        else if (mode && !strcmp(mode, "candycane"))
            candycane(frame++);
        else if (mode && !strcmp(mode, "fire"))
            ringfire(frame++);
        else if (mode && !strcmp(mode, "scoot"))
            scoot(frame++);
        else if (mode && !strcmp(mode, "interactive"))
            interactive();
        else if (mode && !strncmp(mode, "color:", 6)) {
            plain_color(mode+6);
            running = 0;
        }
        else if (mode && !strncmp(mode, "pattern:", 8)) {
            pattern(mode+8);
            running = 0;
        }
        else
            sparkle();

        if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
        {
            fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
            break;
        }

        usleep(1000000 / fps);
    }

    if (clear_on_exit) {
    matrix_clear();
    matrix_render();
    ws2811_render(&ledstring);
    }

    ws2811_fini(&ledstring);

    printf ("\n");
    return ret;
}
