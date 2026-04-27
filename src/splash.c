// SPDX-FileCopyrightText: 2026 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//
// Best-effort framebuffer splash screen for /dev/fb0. Reads a strict subset
// of P6 PPM (binary RGB, maxval 255) and blits it centered. All failures
// result in a silent skip; nothing here is allowed to fail boot.

#include "erlinit.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__

// The splash uses Linux framebuffer ioctls. Provide a no-op stub for the
// macOS test build so erlinit links; in practice run_splash is never reached
// on macOS because the parent only forks when the splash file exists and the
// fixture rewrites /dev paths to a sandbox.
void run_splash(const char *path)
{
    (void) path;
    _exit(0);
}

#else

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/fb.h>
#include <linux/kd.h>

#define SPLASH_MAX_DIM 4096
#define SPLASH_POLL_MS 50

// Skip whitespace and PPM '#' comments between header tokens.
static int ppm_skip_ws(FILE *fp)
{
    int c;
    for (;;) {
        c = fgetc(fp);
        if (c == EOF)
            return -1;
        if (c == '#') {
            while (c != EOF && c != '\n')
                c = fgetc(fp);
        } else if (!isspace(c)) {
            ungetc(c, fp);
            return 0;
        }
    }
}

static int ppm_read_uint(FILE *fp, unsigned int *out)
{
    if (ppm_skip_ws(fp) < 0)
        return -1;
    unsigned int v = 0;
    int digits = 0;
    int c;
    while ((c = fgetc(fp)) != EOF && isdigit(c)) {
        v = v * 10 + (c - '0');
        if (v > SPLASH_MAX_DIM)
            return -1;
        digits++;
    }
    if (!digits)
        return -1;
    *out = v;
    return 0;
}

// Read a strict P6 PPM. Returns malloc'd RGB buffer or NULL on any failure.
static unsigned char *ppm_read(const char *path, unsigned int *w, unsigned int *h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    unsigned char *pixels = NULL;
    char magic[2];
    unsigned int width, height, maxval;

    if (fread(magic, 1, 2, fp) != 2 || magic[0] != 'P' || magic[1] != '6')
        goto out;
    if (ppm_read_uint(fp, &width) < 0 ||
        ppm_read_uint(fp, &height) < 0 ||
        ppm_read_uint(fp, &maxval) < 0)
        goto out;
    if (maxval != 255 || width == 0 || height == 0)
        goto out;

    // Exactly one byte of whitespace separates header from binary data.
    int sep = fgetc(fp);
    if (!isspace(sep))
        goto out;

    size_t bytes = (size_t)width * height * 3;
    pixels = malloc(bytes);
    if (!pixels)
        goto out;
    if (fread(pixels, 1, bytes, fp) != bytes) {
        free(pixels);
        pixels = NULL;
        goto out;
    }
    *w = width;
    *h = height;

out:
    fclose(fp);
    return pixels;
}

static int open_fb_with_timeout(int timeout_ms)
{
    int waited = 0;
    for (;;) {
        int fd = open("/dev/fb0", O_RDWR);
        if (fd >= 0)
            return fd;
        if (waited >= timeout_ms)
            return -1;
        usleep(SPLASH_POLL_MS * 1000);
        waited += SPLASH_POLL_MS;
    }
}

static inline uint32_t pack_pixel(unsigned char r, unsigned char g, unsigned char b,
                                  const struct fb_var_screeninfo *vi)
{
    uint32_t rv = ((uint32_t)r >> (8 - vi->red.length))   << vi->red.offset;
    uint32_t gv = ((uint32_t)g >> (8 - vi->green.length)) << vi->green.offset;
    uint32_t bv = ((uint32_t)b >> (8 - vi->blue.length))  << vi->blue.offset;
    return rv | gv | bv;
}

static void store_pixel(unsigned char *dst, uint32_t px, int bpp)
{
    if (bpp == 16) {
        *(uint16_t *)dst = (uint16_t)px;
    } else if (bpp == 24) {
        dst[0] = px & 0xff;
        dst[1] = (px >> 8) & 0xff;
        dst[2] = (px >> 16) & 0xff;
    } else {
        *(uint32_t *)dst = px;
    }
}

void run_splash(const char *path)
{
    unsigned int img_w, img_h;
    unsigned char *img = ppm_read(path, &img_w, &img_h);
    if (!img)
        _exit(0);

    int fb = open_fb_with_timeout(options.splash_fb_timeout_ms);
    if (fb < 0) {
        elog(ELOG_DEBUG, "splash: /dev/fb0 unavailable after %d ms", options.splash_fb_timeout_ms);
        _exit(0);
    }

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vi) < 0 ||
        ioctl(fb, FBIOGET_FSCREENINFO, &fi) < 0)
        _exit(0);

    int bpp = vi.bits_per_pixel;
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        elog(ELOG_DEBUG, "splash: unsupported bpp %d", bpp);
        _exit(0);
    }

    size_t fb_size = (size_t)fi.line_length * vi.yres;
    unsigned char *fbmem = mmap(NULL, fb_size, PROT_WRITE, MAP_SHARED, fb, 0);
    if (fbmem == MAP_FAILED)
        _exit(0);

    // Suppress fbcon. If this fails we still draw — fbcon may overdraw, but
    // a possibly-overdrawn splash is better than no splash.
    int tty = open("/dev/tty0", O_RDWR);
    if (tty >= 0) {
        if (ioctl(tty, KDSETMODE, KD_GRAPHICS) < 0)
            elog(ELOG_WARNING, "splash: KDSETMODE failed");
        close(tty);
    }

    unsigned int draw_w = img_w < vi.xres ? img_w : vi.xres;
    unsigned int draw_h = img_h < vi.yres ? img_h : vi.yres;
    unsigned int x0 = (vi.xres - draw_w) / 2;
    unsigned int y0 = (vi.yres - draw_h) / 2;
    int Bpp = bpp / 8;
    uint32_t margin = pack_pixel(img[0], img[1], img[2], &vi);

    for (unsigned int y = 0; y < vi.yres; y++) {
        unsigned char *row = fbmem + (size_t)y * fi.line_length;
        for (unsigned int x = 0; x < vi.xres; x++)
            store_pixel(row + x * Bpp, margin, bpp);
    }
    for (unsigned int y = 0; y < draw_h; y++) {
        const unsigned char *src = img + (size_t)y * img_w * 3;
        unsigned char *dst = fbmem + (size_t)(y0 + y) * fi.line_length + x0 * Bpp;
        for (unsigned int x = 0; x < draw_w; x++) {
            store_pixel(dst, pack_pixel(src[0], src[1], src[2], &vi), bpp);
            src += 3;
            dst += Bpp;
        }
    }

    munmap(fbmem, fb_size);
    close(fb);
    free(img);
    _exit(0);
}

#endif // __APPLE__
