/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_fb_rectangle - fill a rectangle with one RGB565 colour over a known
 * background and verify the whole frame.
 *
 * ORACLE: RELATIONSHIP + INVARIANT
 * SOURCE: fbdev mmap readback contract (rectangle pixels read back as the
 *         fill colour) and the definition of a rectangle write (pixels outside
 *         the rectangle must keep the background value).
 * EXPECTED: every pixel inside the rectangle equals <color> AND every pixel
 *           outside equals <background>.
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

static const char *const k_name = "test_fb_rectangle";
static const char *const k_oracle = ORACLE_RELATIONSHIP " + " ORACLE_INVARIANT;
static const char *const k_source =
    "fbdev mmap readback contract + rectangle write invariant";
static const char *const k_expected =
    "pixels inside the rectangle read back as <color>; pixels outside keep "
    "<background>";

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
  unsigned long x = 0, y = 0, w = 0, h = 0;
  unsigned long fg = 0xf800, bg = 0x001f;
  char err[256] = "";
  char obs[320];
  char detail[256];
  size_t inside_bad, outside_bad;
  int rc, i;

  for (i = 1; i < argc; i++) {
    unsigned long *dst = NULL;

    if (test_opt_parse(&opt, argv[i]))
      continue;
    if (strcmp(argv[i], "--x") == 0)
      dst = &x;
    else if (strcmp(argv[i], "--y") == 0)
      dst = &y;
    else if (strcmp(argv[i], "--w") == 0)
      dst = &w;
    else if (strcmp(argv[i], "--h") == 0)
      dst = &h;
    else if (strcmp(argv[i], "--color") == 0)
      dst = &fg;
    else if (strcmp(argv[i], "--background") == 0)
      dst = &bg;
    else if (argv[i][0] == '-') {
      fprintf(stderr, "%s: unknown option '%s' (see --help)\n", k_name,
              argv[i]);
      return TEST_INVALID_USAGE;
    } else {
      dev = argv[i];
      continue;
    }

    if (i + 1 >= argc) {
      fprintf(stderr, "%s: option '%s' needs a value (see --help)\n", k_name,
              argv[i]);
      return TEST_INVALID_USAGE;
    }
    if (dst == &fg || dst == &bg) {
      if (parse_u32(argv[++i], dst, 16) < 0 || *dst > 0xffff)
        return TEST_INVALID_USAGE;
    } else {
      if (parse_u32(argv[++i], dst, 10) < 0)
        return TEST_INVALID_USAGE;
    }
  }
  if (opt.help)
    test_usage(k_name,
               "[options] [fb_device]\n"
               "  --x N --y N --w N --h N   rectangle (default: top-left "
               "half)\n"
               "  --color HEX               fill colour (default f800)\n"
               "  --background HEX          background (default 001f)",
               k_oracle, k_source, k_expected);

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
  if (w == 0)
    w = fb.var.xres / 2;
  if (h == 0)
    h = fb.var.yres / 2;
  if (w == 0 || h == 0 || x + w > fb.var.xres || y + h > fb.var.yres) {
    snprintf(obs, sizeof(obs), "rectangle %lux%lu+%lu+%lu outside %ux%u", w, h,
             x, y, fb.var.xres, fb.var.yres);
    fbdev_close(&fb);
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_INVALID_USAGE, "rectangle out of bounds");
  }

  fbdev_fill(&fb, (uint16_t)bg);
  fbdev_fill_rect(&fb, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
                  (uint16_t)fg);
  inside_bad = fbdev_rect_mismatch(&fb, (unsigned)x, (unsigned)y,
                                   (unsigned)w, (unsigned)h, (uint16_t)fg);
  outside_bad = fbdev_outside_mismatch(&fb, (unsigned)x, (unsigned)y,
                                       (unsigned)w, (unsigned)h, (uint16_t)bg);

  snprintf(obs, sizeof(obs),
           "rect %lux%lu+%lu+%lu fg=0x%04lx over bg=0x%04lx on %ux%u; "
           "inside_mismatch=%zu outside_mismatch=%zu",
           w, h, x, y, fg, bg, fb.var.xres, fb.var.yres, inside_bad,
           outside_bad);
  snprintf(detail, sizeof(detail), "%s %ux%u %ubpp", dev, fb.var.xres,
           fb.var.yres, fb.var.bits_per_pixel);
  fbdev_close(&fb);

  if (inside_bad != 0)
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_FAIL, "rectangle interior does not match the fill");
  if (outside_bad != 0)
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_FAIL, "rectangle write changed pixels outside it");
  return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                     TEST_PASS, detail);
}
