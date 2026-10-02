/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_fb_fill - fill the whole framebuffer with an RGB565 colour and read it
 * back.
 *
 * ORACLE: RELATIONSHIP
 * SOURCE: fbdev mmap contract - a framebuffer mapping shared with the kernel
 *         must return the bytes userspace wrote.
 * EXPECTED: every pixel of the 128x160 RGB565 frame reads back as the written
 *           colour (0 mismatches).
 *
 * The 16 bpp RGB565 requirement is a SPEC coming from drm.c:
 * mode_config.preferred_depth = 16 and the plane advertises DRM_FORMAT_RGB565.
 * A device that does not match is an unsuitable environment, not a FAIL.
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

static const char *const k_name = "test_fb_fill";
static const char *const k_oracle = ORACLE_RELATIONSHIP;
static const char *const k_source =
    "fbdev mmap readback contract (write then read the same mapping)";
static const char *const k_expected =
    "every RGB565 pixel of the frame reads back as the written colour "
    "(0 mismatches)";

int main(int argc, char **argv)
{
  struct test_options opt = { 0 };
  struct fbdev fb;
  const char *dev = "/dev/fb0";
  const char *pos[2] = { NULL, NULL };
  int npos = 0;
  char color_arg[64] = "";
  char *end = NULL;
  unsigned long color;
  char err[256] = "";
  char obs[256];
  char detail[256];
  size_t mismatches;
  int rc, i;

  for (i = 1; i < argc; i++) {
    if (test_opt_parse(&opt, argv[i]))
      continue;
    if (npos < 2)
      pos[npos++] = argv[i];
    else
      npos++;
  }
  if (opt.help)
    test_usage(k_name, "[options] [fb_device] <color>\n"
                       "  <color>  RGB565 value in hex, e.g. f800 or 0x07e0",
               k_oracle, k_source, k_expected);
  if (npos > 2) {
    fprintf(stderr, "%s: too many arguments (see --help)\n", k_name);
    return TEST_INVALID_USAGE;
  }
  if (npos == 0) {
    fprintf(stderr, "%s: missing <color> (see --help)\n", k_name);
    return TEST_INVALID_USAGE;
  }
  if (npos == 2)
    dev = pos[0];
  snprintf(color_arg, sizeof(color_arg), "%s", pos[npos - 1]);

  errno = 0;
  color = strtoul(color_arg, &end, 16);
  if (end == color_arg || *end != '\0' || errno != 0 || color > 0xffff) {
    fprintf(stderr, "%s: invalid RGB565 colour '%s' (see --help)\n", k_name,
            color_arg);
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

  fbdev_fill(&fb, (uint16_t)color);
  mismatches = fbdev_rect_mismatch(&fb, 0, 0, fb.var.xres, fb.var.yres,
                                   (uint16_t)color);

  snprintf(obs, sizeof(obs),
           "filled %ux%u with 0x%04lx; readback mismatches=%zu", fb.var.xres,
           fb.var.yres, color, mismatches);
  snprintf(detail, sizeof(detail), "%s %ux%u %ubpp stride=%zu", dev,
           fb.var.xres, fb.var.yres, fb.var.bits_per_pixel, fb.stride);
  fbdev_close(&fb);

  return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                     mismatches == 0 ? TEST_PASS : TEST_FAIL, detail);
}
