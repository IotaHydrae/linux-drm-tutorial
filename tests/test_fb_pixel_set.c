/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_fb_pixel_set - set one RGB565 pixel and verify it, and that a disjoint
 * sentinel pixel is untouched.
 *
 * ORACLE: RELATIONSHIP + INVARIANT
 * SOURCE: fbdev mmap readback contract (the written pixel reads back) and the
 *         definition of a single-pixel write (no other pixel may change).
 * EXPECTED: pixel (x,y) reads back as <color> AND the sentinel pixel keeps its
 *           previous value.
 *
 * 16 bpp RGB565 is a SPEC from drm.c (preferred_depth = 16, plane format
 * DRM_FORMAT_RGB565); a non-matching device is an unsuitable environment.
 *
 * Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
 *             4 TIMEOUT, 5 INCONCLUSIVE.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbdev.h"
#include "test_common.h"

static const char *const k_name = "test_fb_pixel_set";
static const char *const k_oracle = ORACLE_RELATIONSHIP " + " ORACLE_INVARIANT;
static const char *const k_source =
    "fbdev mmap readback contract + single-pixel write invariant";
static const char *const k_expected =
    "target pixel reads back as <color> and a disjoint sentinel pixel is "
    "unchanged";

static int parse_u32(const char *s, unsigned long *out, int base)
{
  char *end = NULL;

  errno = 0;
  *out = strtoul(s, &end, base);
  if (end == s || *end != '\0' || errno != 0)
    return -1;
  return 0;
}

int main(int argc, char **argv)
{
  struct test_options opt = { 0 };
  struct fbdev fb;
  const char *dev = "/dev/fb0";
  const char *pos[4] = { NULL, NULL, NULL, NULL };
  int npos = 0;
  unsigned long x, y, color;
  char err[256] = "";
  char obs[320];
  char detail[256];
  uint16_t before_target, sentinel_before, sentinel_after;
  unsigned sx, sy;
  int rc, i;

  for (i = 1; i < argc; i++) {
    if (test_opt_parse(&opt, argv[i]))
      continue;
    if (npos < 4)
      pos[npos++] = argv[i];
    else
      npos++;
  }
  if (opt.help)
    test_usage(k_name,
               "[options] [fb_device] <x> <y> <color>\n"
               "  x, y     decimal coordinates\n"
               "  <color>  RGB565 value in hex, e.g. f800",
               k_oracle, k_source, k_expected);
  if (npos > 4) {
    fprintf(stderr, "%s: too many arguments (see --help)\n", k_name);
    return TEST_INVALID_USAGE;
  }
  if (npos < 3) {
    fprintf(stderr, "%s: need <x> <y> <color> (see --help)\n", k_name);
    return TEST_INVALID_USAGE;
  }
  if (npos == 4)
    dev = pos[0];
  if (parse_u32(pos[npos - 3], &x, 10) < 0 ||
      parse_u32(pos[npos - 2], &y, 10) < 0 ||
      parse_u32(pos[npos - 1], &color, 16) < 0 || color > 0xffff) {
    fprintf(stderr, "%s: invalid x/y/color (see --help)\n", k_name);
    return TEST_INVALID_USAGE;
  }

  rc = fbdev_open(&fb, dev, err, sizeof(err));
  if (rc != FBDEV_OK) {
    snprintf(obs, sizeof(obs), "cannot open/map %s", dev);
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_ENVIRONMENT_ERROR, err);
  }
  if (!fbdev_is_rgb565(&fb)) {
    snprintf(obs, sizeof(obs), "%s is %ux%u %ubpp (not RGB565)", dev,
             fb.var.xres, fb.var.yres, fb.var.bits_per_pixel);
    fbdev_close(&fb);
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_ENVIRONMENT_ERROR,
                       "device is not a 16 bpp RGB565 framebuffer");
  }
  if (x >= fb.var.xres || y >= fb.var.yres) {
    snprintf(obs, sizeof(obs), "(%lu,%lu) outside %ux%u", x, y, fb.var.xres,
             fb.var.yres);
    fbdev_close(&fb);
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_INVALID_USAGE, "coordinate out of bounds");
  }

  /* A sentinel at the diagonal opposite of the target, always disjoint. */
  sx = (unsigned)(x + fb.var.xres / 2) % fb.var.xres;
  sy = (unsigned)(y + fb.var.yres / 2) % fb.var.yres;
  if (sx == x && sy == y)
    sx = (unsigned)(x + 1) % fb.var.xres;

  before_target = fbdev_get_pixel(&fb, (unsigned)x, (unsigned)y);
  sentinel_before = fbdev_get_pixel(&fb, sx, sy);

  fbdev_set_pixel(&fb, (unsigned)x, (unsigned)y, (uint16_t)color);

  {
    uint16_t after_target = fbdev_get_pixel(&fb, (unsigned)x, (unsigned)y);

    sentinel_after = fbdev_get_pixel(&fb, sx, sy);
    snprintf(obs, sizeof(obs),
             "(%lu,%lu): 0x%04x -> 0x%04lx (readback 0x%04x); sentinel "
             "(%u,%u) 0x%04x -> 0x%04x",
             x, y, before_target, color, after_target, sx, sy, sentinel_before,
             sentinel_after);
    snprintf(detail, sizeof(detail), "%s %ux%u %ubpp", dev, fb.var.xres,
             fb.var.yres, fb.var.bits_per_pixel);

    if (after_target != (uint16_t)color) {
      fbdev_close(&fb);
      return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected,
                         obs, TEST_FAIL,
                         "target pixel did not read back as written");
    }
    if (sentinel_after != sentinel_before) {
      fbdev_close(&fb);
      return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected,
                         obs, TEST_FAIL,
                         "a single-pixel write disturbed a disjoint pixel");
    }
  }

  fbdev_close(&fb);
  return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                     TEST_PASS, detail);
}
