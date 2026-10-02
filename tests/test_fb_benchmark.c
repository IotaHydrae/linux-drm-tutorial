/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_fb_benchmark - random RGB565 rectangle throughput, ported from the
 * original `fb_rectangle` framebuffer benchmark.
 *
 * ORACLE: NONE
 * SOURCE: none - the project defines no target throughput for this driver, so
 *         there is no threshold to compare against.
 * EXPECTED: n/a; this test reports an observation only and therefore always
 *           exits INCONCLUSIVE (5). Do not add a target value here unless a
 *           documented requirement or baseline exists.
 *
 * It writes random rectangles of (width/4)x(height/4) pixels for --duration
 * milliseconds and reports MPixels/second. Environment variables WIDTH,
 * HEIGHT, POSX and POSY (the original interface) are still honoured as
 * defaults.
 *
 * Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
 *             4 TIMEOUT, 5 INCONCLUSIVE.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "fbdev.h"
#include "test_common.h"

static const char *const k_name = "test_fb_benchmark";
static const char *const k_oracle = ORACLE_NONE;
static const char *const k_source =
    "no throughput requirement is defined for this driver";
static const char *const k_expected =
    "observation only (no oracle) - result is INCONCLUSIVE";

static int parse_u32(const char *s, unsigned long *out, int base)
{
  char *end = NULL;

  errno = 0;
  *out = strtoul(s, &end, base);
  if (end == s || *end != '\0' || errno != 0)
    return -1;
  return 0;
}

static unsigned long env_or(const char *name, unsigned long fallback)
{
  const char *v = getenv(name);
  unsigned long out;

  if (!v || !*v || parse_u32(v, &out, 10) < 0)
    return fallback;
  return out;
}

static unsigned long now_ms(void)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)tv.tv_usec / 1000;
}

int main(int argc, char **argv)
{
  struct test_options opt = { 0 };
  struct fbdev fb;
  const char *dev = "/dev/fb0";
  unsigned long duration = 5000, width = 0, height = 0, posx = 0, posy = 0;
  char err[256] = "";
  char obs[256];
  char detail[256];
  unsigned long start, end, count = 0;
  unsigned w, h;
  int rc, i;

  for (i = 1; i < argc; i++) {
    unsigned long *dst = NULL;

    if (test_opt_parse(&opt, argv[i]))
      continue;
    if (strcmp(argv[i], "--duration") == 0)
      dst = &duration;
    else if (strcmp(argv[i], "--width") == 0)
      dst = &width;
    else if (strcmp(argv[i], "--height") == 0)
      dst = &height;
    else if (strcmp(argv[i], "--posx") == 0)
      dst = &posx;
    else if (strcmp(argv[i], "--posy") == 0)
      dst = &posy;
    else if (argv[i][0] == '-') {
      fprintf(stderr, "%s: unknown option '%s' (see --help)\n", k_name,
              argv[i]);
      return TEST_INVALID_USAGE;
    } else {
      dev = argv[i];
      continue;
    }
    if (i + 1 >= argc || parse_u32(argv[++i], dst, 10) < 0) {
      fprintf(stderr, "%s: bad value for option (see --help)\n", k_name);
      return TEST_INVALID_USAGE;
    }
  }
  if (opt.help)
    test_usage(k_name,
               "[options] [fb_device]\n"
               "  --duration MS   measurement window (default 5000)\n"
               "  --width N --height N   region to spray (default: frame)\n"
               "  --posx N --posy N      region origin (default 0 0)\n"
               "  env WIDTH/HEIGHT/POSX/POSY are accepted as defaults",
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

  if (width == 0)
    width = env_or("WIDTH", fb.var.xres);
  if (height == 0)
    height = env_or("HEIGHT", fb.var.yres);
  posx = env_or("POSX", posx);
  posy = env_or("POSY", posy);
  if (width > fb.var.xres)
    width = fb.var.xres;
  if (height > fb.var.yres)
    height = fb.var.yres;
  if (posx + width > fb.var.xres || posy + height > fb.var.yres ||
      width < 4 || height < 4) {
    snprintf(obs, sizeof(obs), "region %lux%lu+%lu+%lu outside %ux%u", width,
             height, posx, posy, fb.var.xres, fb.var.yres);
    fbdev_close(&fb);
    return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                       TEST_INVALID_USAGE, "region out of bounds");
  }

  w = (unsigned)width >> 2;
  h = (unsigned)height >> 2;
  start = now_ms();
  do {
    uint16_t color = (uint16_t)rand();
    unsigned rx = (unsigned)(rand() % (int)(width - w));
    unsigned ry = (unsigned)(rand() % (int)(height - h));

    fbdev_fill_rect(&fb, (unsigned)posx + rx, (unsigned)posy + ry, w, h,
                    color);
    count++;
    end = now_ms();
    fbdev_pan(&fb);
  } while (end < start + duration);

  snprintf(obs, sizeof(obs),
           "%lu frames of %ux%u in %lu ms = %.2f MPixels/s", count, w, h,
           end - start,
           (end - start) ? (double)count * w * h / ((double)(end - start) * 1000.0)
                         : 0.0);
  snprintf(detail, sizeof(detail), "%s %ux%u %ubpp", dev, fb.var.xres,
           fb.var.yres, fb.var.bits_per_pixel);
  fbdev_close(&fb);

  return test_report(&opt, k_name, dev, k_oracle, k_source, k_expected, obs,
                     TEST_INCONCLUSIVE, detail);
}
