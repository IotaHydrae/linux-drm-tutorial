/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fbdev - reusable access to a Linux framebuffer (/dev/fbN).
 *
 * This is the "tool" layer for the framebuffer tests: it only acquires facts
 * (open the device, map it, read/write pixels, count mismatches). It does not
 * decide PASS/FAIL - the tests in tests/ do that.
 *
 * Only 16 bpp RGB565 is supported, because that is what this driver exposes
 * (drm.c sets mode_config.preferred_depth = 16 and the plane advertises
 * DRM_FORMAT_RGB565).
 */
#ifndef FBDEV_H
#define FBDEV_H

#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum fbdev_result {
  FBDEV_OK = 0,
  FBDEV_ERR_OPEN = 1,
  FBDEV_ERR_IOCTL = 2,
  FBDEV_ERR_MAP = 3,
  FBDEV_ERR_UNSUPPORTED = 4,
};

struct fbdev {
  char path[128];
  int fd;
  struct fb_var_screeninfo var;
  size_t stride;   /* bytes per scanline (line_length, or xres * bpp / 8) */
  size_t len;      /* mapped length in bytes */
  uint8_t *map;
};

/*
 * Open `path`, read FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO and mmap the
 * frame. On failure returns a negative enum fbdev_result and writes a short
 * reason into errbuf.
 */
int fbdev_open(struct fbdev *fb, const char *path, char *errbuf,
               size_t errbuf_len);

/* Unmap and close. Safe to call on a zeroed/closed struct. */
void fbdev_close(struct fbdev *fb);

/* True when the device is 16 bpp with the standard 5-6-5 RGB layout. */
bool fbdev_is_rgb565(const struct fbdev *fb);

/* 16 bpp accessors. Bounds must be checked by the caller. */
uint16_t fbdev_get_pixel(const struct fbdev *fb, unsigned x, unsigned y);
void fbdev_set_pixel(struct fbdev *fb, unsigned x, unsigned y, uint16_t color);

void fbdev_fill(struct fbdev *fb, uint16_t color);
void fbdev_fill_rect(struct fbdev *fb, unsigned x, unsigned y, unsigned w,
                     unsigned h, uint16_t color);

/* Count pixels in the rectangle that are not `expected`. */
size_t fbdev_rect_mismatch(const struct fbdev *fb, unsigned x, unsigned y,
                           unsigned w, unsigned h, uint16_t expected);
/* Count pixels outside the rectangle that are not `expected`. */
size_t fbdev_outside_mismatch(const struct fbdev *fb, unsigned x, unsigned y,
                              unsigned w, unsigned h, uint16_t expected);

/* FBIOPAN_DISPLAY; returns 0 on success, -1 with errno set otherwise. */
int fbdev_pan(struct fbdev *fb);

/* Pack 8-bit components into an RGB565 value. */
static inline uint16_t fbdev_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

#endif /* FBDEV_H */
