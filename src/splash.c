// SPDX-FileCopyrightText: 2026 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//
// Best-effort framebuffer splash screen for /dev/fb0. Reads a strict subset
// of P6 PPM (binary RGB, maxval 255) and blits it centered. All failures
// result in a silent skip; nothing here is allowed to fail boot.

#include "erlinit.h"

#include <ctype.h>
#include <errno.h>
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
    if (!fp) {
        elog(ELOG_DEBUG, "splash: fopen(%s) failed: %s", path, strerror(errno));
        return NULL;
    }

    unsigned char *pixels = NULL;
    char magic[2];
    unsigned int width = 0, height = 0, maxval = 0;

    if (fread(magic, 1, 2, fp) != 2 || magic[0] != 'P' || magic[1] != '6') {
        elog(ELOG_DEBUG, "splash: %s: not a P6 PPM (got '%c%c')", path, magic[0], magic[1]);
        goto out;
    }
    if (ppm_read_uint(fp, &width) < 0 ||
        ppm_read_uint(fp, &height) < 0 ||
        ppm_read_uint(fp, &maxval) < 0) {
        elog(ELOG_DEBUG, "splash: %s: bad header (w=%u h=%u maxval=%u)", path, width, height, maxval);
        goto out;
    }
    if (maxval != 255 || width == 0 || height == 0) {
        elog(ELOG_DEBUG, "splash: %s: rejected dims w=%u h=%u maxval=%u (need maxval=255, dims 1..%d)",
             path, width, height, maxval, SPLASH_MAX_DIM);
        goto out;
    }

    // Exactly one byte of whitespace separates header from binary data.
    int sep = fgetc(fp);
    if (!isspace(sep)) {
        elog(ELOG_DEBUG, "splash: %s: missing whitespace after header (got 0x%02x)", path, sep);
        goto out;
    }

    size_t bytes = (size_t)width * height * 3;
    pixels = malloc(bytes);
    if (!pixels) {
        elog(ELOG_DEBUG, "splash: malloc %zu bytes failed", bytes);
        goto out;
    }
    size_t got = fread(pixels, 1, bytes, fp);
    if (got != bytes) {
        elog(ELOG_DEBUG, "splash: %s: short read %zu/%zu bytes", path, got, bytes);
        free(pixels);
        pixels = NULL;
        goto out;
    }
    *w = width;
    *h = height;
    elog(ELOG_DEBUG, "splash: parsed %s -> %ux%u (%zu bytes)", path, width, height, bytes);

out:
    fclose(fp);
    return pixels;
}

static int open_fb_with_timeout(int timeout_ms)
{
    int waited = 0;
    int last_errno = 0;
    elog(ELOG_DEBUG, "splash: polling /dev/fb0 (timeout %d ms)", timeout_ms);
    for (;;) {
        int fd = open("/dev/fb0", O_RDWR);
        if (fd >= 0) {
            elog(ELOG_DEBUG, "splash: opened /dev/fb0 after %d ms (fd=%d)", waited, fd);
            return fd;
        }
        last_errno = errno;
        if (waited >= timeout_ms) {
            elog(ELOG_DEBUG, "splash: /dev/fb0 unavailable after %d ms (last errno: %s)",
                 timeout_ms, strerror(last_errno));
            return -1;
        }
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
    elog(ELOG_DEBUG, "splash: run_splash entered, path=%s, fb_timeout_ms=%d, pid=%d",
         path, options.splash_fb_timeout_ms, (int) getpid());

    unsigned int img_w, img_h;
    unsigned char *img = ppm_read(path, &img_w, &img_h);
    if (!img) {
        elog(ELOG_DEBUG, "splash: ppm_read failed, exiting");
        _exit(0);
    }

    int fb = open_fb_with_timeout(options.splash_fb_timeout_ms);
    if (fb < 0)
        _exit(0);

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vi) < 0) {
        elog(ELOG_DEBUG, "splash: FBIOGET_VSCREENINFO failed: %s", strerror(errno));
        _exit(0);
    }
    if (ioctl(fb, FBIOGET_FSCREENINFO, &fi) < 0) {
        elog(ELOG_DEBUG, "splash: FBIOGET_FSCREENINFO failed: %s", strerror(errno));
        _exit(0);
    }
    elog(ELOG_DEBUG, "splash: vinfo bpp=%u xres=%u yres=%u xres_v=%u yres_v=%u xoff=%u yoff=%u",
         vi.bits_per_pixel, vi.xres, vi.yres, vi.xres_virtual, vi.yres_virtual,
         vi.xoffset, vi.yoffset);
    elog(ELOG_DEBUG, "splash: vinfo r=%u/%u g=%u/%u b=%u/%u a=%u/%u nonstd=%u",
         vi.red.offset, vi.red.length,
         vi.green.offset, vi.green.length,
         vi.blue.offset, vi.blue.length,
         vi.transp.offset, vi.transp.length,
         vi.nonstd);
    elog(ELOG_DEBUG, "splash: finfo line_length=%u smem_len=%u type=%u visual=%u",
         fi.line_length, fi.smem_len, fi.type, fi.visual);

    int bpp = vi.bits_per_pixel;
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        elog(ELOG_DEBUG, "splash: unsupported bpp %d, skipping", bpp);
        _exit(0);
    }

    size_t fb_size = (size_t)fi.line_length * vi.yres;
    elog(ELOG_DEBUG, "splash: mmap %zu bytes (line_length=%u * yres=%u)",
         fb_size, fi.line_length, vi.yres);
    unsigned char *fbmem = mmap(NULL, fb_size, PROT_WRITE, MAP_SHARED, fb, 0);
    if (fbmem == MAP_FAILED) {
        elog(ELOG_DEBUG, "splash: mmap failed: %s", strerror(errno));
        _exit(0);
    }
    elog(ELOG_DEBUG, "splash: mmap ok at %p", (void *) fbmem);

    // Suppress fbcon. If this fails we still draw — fbcon may overdraw, but
    // a possibly-overdrawn splash is better than no splash.
    int tty = open("/dev/tty0", O_RDWR);
    if (tty >= 0) {
        if (ioctl(tty, KDSETMODE, KD_GRAPHICS) < 0)
            elog(ELOG_WARNING, "splash: KDSETMODE failed: %s", strerror(errno));
        else
            elog(ELOG_DEBUG, "splash: /dev/tty0 set to KD_GRAPHICS");
        close(tty);
    } else {
        elog(ELOG_DEBUG, "splash: cannot open /dev/tty0: %s", strerror(errno));
    }

    unsigned int draw_w = img_w < vi.xres ? img_w : vi.xres;
    unsigned int draw_h = img_h < vi.yres ? img_h : vi.yres;
    unsigned int x0 = (vi.xres - draw_w) / 2;
    unsigned int y0 = (vi.yres - draw_h) / 2;
    int Bpp = bpp / 8;
    uint32_t margin = pack_pixel(img[0], img[1], img[2], &vi);
    elog(ELOG_DEBUG, "splash: blitting img %ux%u -> draw %ux%u at (%u,%u), Bpp=%d, margin_rgb=%02x%02x%02x packed=0x%08x",
         img_w, img_h, draw_w, draw_h, x0, y0, Bpp,
         img[0], img[1], img[2], margin);

    for (unsigned int y = 0; y < vi.yres; y++) {
        unsigned char *row = fbmem + (size_t)y * fi.line_length;
        for (unsigned int x = 0; x < vi.xres; x++)
            store_pixel(row + x * Bpp, margin, bpp);
    }
    elog(ELOG_DEBUG, "splash: margin fill complete");

    for (unsigned int y = 0; y < draw_h; y++) {
        const unsigned char *src = img + (size_t)y * img_w * 3;
        unsigned char *dst = fbmem + (size_t)(y0 + y) * fi.line_length + x0 * Bpp;
        for (unsigned int x = 0; x < draw_w; x++) {
            store_pixel(dst, pack_pixel(src[0], src[1], src[2], &vi), bpp);
            src += 3;
            dst += Bpp;
        }
    }
    elog(ELOG_DEBUG, "splash: image blit complete, exiting");

    munmap(fbmem, fb_size);
    close(fb);
    free(img);
    _exit(0);
}

#endif // __APPLE__
