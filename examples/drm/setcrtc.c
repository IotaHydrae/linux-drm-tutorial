/*
  drm_setcrtc - set a mode with the legacy DRM_IOCTL_MODE_SETCRTC path

  Usage: sudo ./drm_setcrtc.out [/dev/dri/card0]

  Steps: GETRESOURCES -> GETCONNECTOR (pick the preferred mode) ->
  CREATE_DUMB (RGB565) -> ADDFB2 -> MAP_DUMB (fill a pattern) -> SETCRTC.

  With the tutorial driver, watch dmesg: the legacy SETCRTC ioctl is
  translated by drm_atomic_helper_set_config() into the very same atomic
  commit path as the fbdev writes, so you see atomic_check,
  atomic_update and atomic_enable logs.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

static int drm_ioctl(int fd, unsigned long req, void *arg)
{
  int ret;

  do {
    ret = ioctl(fd, req, arg);
  } while (ret == -1 && errno == EINTR);

  return ret;
}

static struct drm_mode_modeinfo *pick_mode(struct drm_mode_get_connector *conn)
{
  int i;

  for (i = 0; i < (int)conn->count_modes; i++)
    if (conn->modes_ptr && ((struct drm_mode_modeinfo *)conn->modes_ptr)[i].type
                           & DRM_MODE_TYPE_PREFERRED)
      return &((struct drm_mode_modeinfo *)conn->modes_ptr)[i];

  return conn->count_modes ? (struct drm_mode_modeinfo *)conn->modes_ptr : NULL;
}

static void fill_rgb565_pattern(uint16_t *pixels, uint32_t pitch, uint32_t w,
                                uint32_t h)
{
  uint32_t x, y;

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      uint16_t r = (uint16_t)(x * 31 / (w - 1));
      uint16_t g = (uint16_t)(y * 63 / (h - 1));
      uint16_t b = (uint16_t)((x + y) * 31 / (w + h - 2));

      pixels[y * (pitch / 2) + x] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
}

int main(int argc, char **argv)
{
  const char *dev = argc > 1 ? argv[1] : "/dev/dri/card0";
  struct drm_mode_card_res res = { 0 };
  struct drm_mode_get_connector conn = { 0 };
  struct drm_mode_modeinfo *mode;
  struct drm_mode_create_dumb dumb = { 0 };
  struct drm_mode_fb_cmd2 fb = { 0 };
  struct drm_mode_map_dumb map = { 0 };
  struct drm_mode_crtc set = { 0 };
  uint32_t *fb_ids, *crtc_ids, *conn_ids, *enc_ids;
  uint32_t connector_id, crtc_id;
  uint32_t *props = NULL;
  uint64_t *prop_values = NULL;
  uint16_t *pixels;
  int fd;

  fd = open(dev, O_RDWR);
  if (fd < 0) {
    perror(dev);
    return 1;
  }

  /* SETCRTC requires DRM master (and usually root). */
  if (drm_ioctl(fd, DRM_IOCTL_SET_MASTER, 0) < 0)
    perror("SET_MASTER (ignored)");

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    perror("MODE_GETRESOURCES");
    return 1;
  }
  if (!res.count_connectors || !res.count_crtcs) {
    fprintf(stderr, "No connectors or CRTCs on %s\n", dev);
    return 1;
  }

  fb_ids = calloc(res.count_fbs ? res.count_fbs : 1, sizeof(*fb_ids));
  crtc_ids = calloc(res.count_crtcs ? res.count_crtcs : 1, sizeof(*crtc_ids));
  conn_ids = calloc(res.count_connectors ? res.count_connectors : 1,
                    sizeof(*conn_ids));
  enc_ids = calloc(res.count_encoders ? res.count_encoders : 1,
                   sizeof(*enc_ids));
  res.fb_id_ptr = (uint64_t)(uintptr_t)fb_ids;
  res.crtc_id_ptr = (uint64_t)(uintptr_t)crtc_ids;
  res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
  res.encoder_id_ptr = (uint64_t)(uintptr_t)enc_ids;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    perror("MODE_GETRESOURCES (fill)");
    return 1;
  }
  connector_id = conn_ids[0];
  crtc_id = crtc_ids[0];

  conn.connector_id = connector_id;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
    perror("MODE_GETCONNECTOR");
    return 1;
  }
  conn.modes_ptr = (uint64_t)(uintptr_t)
                   calloc(conn.count_modes ? conn.count_modes : 1,
                          sizeof(struct drm_mode_modeinfo));
  conn.encoders_ptr = (uint64_t)(uintptr_t)
                      calloc(conn.count_encoders ? conn.count_encoders : 1,
                             sizeof(uint32_t));
  props = calloc(conn.count_props ? conn.count_props : 1, sizeof(*props));
  prop_values = calloc(conn.count_props ? conn.count_props : 1,
                       sizeof(*prop_values));
  conn.props_ptr = (uint64_t)(uintptr_t)props;
  conn.prop_values_ptr = (uint64_t)(uintptr_t)prop_values;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
    perror("MODE_GETCONNECTOR (fill)");
    return 1;
  }
  mode = pick_mode(&conn);
  if (!mode) {
    fprintf(stderr, "No mode on connector %u\n", connector_id);
    return 1;
  }
  free(props);
  free(prop_values);

  dumb.width = mode->hdisplay;
  dumb.height = mode->vdisplay;
  dumb.bpp = 16; /* RGB565, matching the tutorial plane */
  if (drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) < 0) {
    perror("MODE_CREATE_DUMB");
    return 1;
  }

  fb.width = dumb.width;
  fb.height = dumb.height;
  fb.pixel_format = DRM_FORMAT_RGB565;
  fb.handles[0] = dumb.handle;
  fb.pitches[0] = dumb.pitch;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) {
    perror("MODE_ADDFB2");
    return 1;
  }

  map.handle = dumb.handle;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
    perror("MODE_MAP_DUMB");
    return 1;
  }
  pixels = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                map.offset);
  if (pixels == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  fill_rgb565_pattern(pixels, dumb.pitch, dumb.width, dumb.height);
  munmap(pixels, dumb.size);

  set.set_connectors_ptr = (uint64_t)(uintptr_t)&connector_id;
  set.count_connectors = 1;
  set.crtc_id = crtc_id;
  set.fb_id = fb.fb_id;
  set.x = 0;
  set.y = 0;
  set.mode_valid = 1;
  set.mode = *mode;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &set) < 0) {
    perror("MODE_SETCRTC");
    return 1;
  }

  printf("SETCRTC: crtc=%u fb=%u mode=%ux%u connector=%u ok\n",
         crtc_id, fb.fb_id, mode->hdisplay, mode->vdisplay, connector_id);
  printf("Check dmesg for atomic_check / atomic_update / atomic_enable\n");

  close(fd);
  return 0;
}
