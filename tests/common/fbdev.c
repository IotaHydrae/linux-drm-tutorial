/* SPDX-License-Identifier: GPL-2.0 */
/* fbdev - see fbdev.h. */

#include "fbdev.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void set_err(char *errbuf, size_t errbuf_len, const char *msg,
                    const char *arg)
{
  if (!errbuf || errbuf_len == 0)
    return;
  snprintf(errbuf, errbuf_len, msg, arg);
}

int fbdev_open(struct fbdev *fb, const char *path, char *errbuf,
               size_t errbuf_len)
{
  struct fb_fix_screeninfo fix;
  size_t bytes_per_pixel;

  memset(fb, 0, sizeof(*fb));
  fb->fd = -1;
  snprintf(fb->path, sizeof(fb->path), "%s", path);

  fb->fd = open(path, O_RDWR);
  if (fb->fd < 0) {
    char buf[256];

    snprintf(buf, sizeof(buf), "%s: %s", path, strerror(errno));
    set_err(errbuf, errbuf_len, "%s", buf);
    return FBDEV_ERR_OPEN;
  }

  if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0) {
    set_err(errbuf, errbuf_len, "FBIOGET_VSCREENINFO: %s",
            strerror(errno));
    fbdev_close(fb);
    return FBDEV_ERR_IOCTL;
  }

  memset(&fix, 0, sizeof(fix));
  if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
    set_err(errbuf, errbuf_len, "FBIOGET_FSCREENINFO: %s",
            strerror(errno));
    fbdev_close(fb);
    return FBDEV_ERR_IOCTL;
  }

  if (fb->var.bits_per_pixel == 0 || fb->var.xres == 0 ||
      fb->var.yres == 0) {
    set_err(errbuf, errbuf_len, "empty framebuffer geometry %ux%u@%ubpp",
            fb->var.xres, fb->var.yres, fb->var.bits_per_pixel);
    fbdev_close(fb);
    return FBDEV_ERR_UNSUPPORTED;
  }

  bytes_per_pixel = (size_t)fb->var.bits_per_pixel / 8;
  if (bytes_per_pixel == 0)
    bytes_per_pixel = 1;
  fb->stride = fix.line_length ? fix.line_length
                               : (size_t)fb->var.xres * bytes_per_pixel;
  fb->len = fb->stride * fb->var.yres;

  fb->map = mmap(NULL, fb->len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd,
                 0);
  if (fb->map == MAP_FAILED) {
    fb->map = NULL;
    set_err(errbuf, errbuf_len, "mmap %zu bytes: %s", strerror(errno));
    fbdev_close(fb);
    return FBDEV_ERR_MAP;
  }

  return FBDEV_OK;
}

void fbdev_close(struct fbdev *fb)
{
  if (fb->map) {
    munmap(fb->map, fb->len);
    fb->map = NULL;
  }
  if (fb->fd >= 0) {
    close(fb->fd);
    fb->fd = -1;
  }
}

bool fbdev_is_rgb565(const struct fbdev *fb)
{
  return fb->var.bits_per_pixel == 16 && fb->var.red.offset == 11 &&
         fb->var.red.length == 5 && fb->var.green.offset == 5 &&
         fb->var.green.length == 6 && fb->var.blue.offset == 0 &&
         fb->var.blue.length == 5;
}

uint16_t fbdev_get_pixel(const struct fbdev *fb, unsigned x, unsigned y)
{
  const uint8_t *p = fb->map + (size_t)y * fb->stride + (size_t)x * 2;

  return (uint16_t)(p[0] | (p[1] << 8));
}

void fbdev_set_pixel(struct fbdev *fb, unsigned x, unsigned y, uint16_t color)
{
  uint8_t *p = fb->map + (size_t)y * fb->stride + (size_t)x * 2;

  p[0] = (uint8_t)(color & 0xff);
  p[1] = (uint8_t)(color >> 8);
}

void fbdev_fill_rect(struct fbdev *fb, unsigned x, unsigned y, unsigned w,
                     unsigned h, uint16_t color)
{
  unsigned i, j;

  for (i = 0; i < h; i++)
    for (j = 0; j < w; j++)
      fbdev_set_pixel(fb, x + j, y + i, color);
}

void fbdev_fill(struct fbdev *fb, uint16_t color)
{
  fbdev_fill_rect(fb, 0, 0, fb->var.xres, fb->var.yres, color);
}

size_t fbdev_rect_mismatch(const struct fbdev *fb, unsigned x, unsigned y,
                           unsigned w, unsigned h, uint16_t expected)
{
  size_t bad = 0;
  unsigned i, j;

  for (i = 0; i < h; i++)
    for (j = 0; j < w; j++)
      if (fbdev_get_pixel(fb, x + j, y + i) != expected)
        bad++;
  return bad;
}

size_t fbdev_outside_mismatch(const struct fbdev *fb, unsigned x, unsigned y,
                              unsigned w, unsigned h, uint16_t expected)
{
  size_t bad = 0;
  unsigned i, j;

  for (i = 0; i < fb->var.yres; i++) {
    for (j = 0; j < fb->var.xres; j++) {
      bool inside = j >= x && j < x + w && i >= y && i < y + h;

      if (!inside && fbdev_get_pixel(fb, j, i) != expected)
        bad++;
    }
  }
  return bad;
}

int fbdev_pan(struct fbdev *fb)
{
  return ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->var);
}
