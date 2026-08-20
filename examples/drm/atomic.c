/*
  drm_atomic - commit a modeset with DRM_IOCTL_MODE_ATOMIC

  Usage: sudo ./drm_atomic.out [/dev/dri/card0]

  Demonstrates the property-based atomic API:
    1. discover property IDs with MODE_OBJ_GETPROPERTIES + MODE_GETPROPERTY
    2. create the mode blob with MODE_CREATEPROPBLOB
    3. commit with MODE_ATOMIC, first TEST_ONLY then for real

  With the tutorial driver, the TEST_ONLY commit only runs atomic_check
  (no atomic_update log); the real commit also runs atomic_update, which
  dumps the first 4x4 pixels - fill the buffer with 0xf800 and you will
  see the red pixels in dmesg.
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

static int find_prop_id(int fd, uint32_t obj_id, uint32_t obj_type,
                        const char *want)
{
  struct drm_mode_obj_get_properties props = {
    .obj_id = obj_id,
    .obj_type = obj_type,
  };
  uint32_t *ids;
  uint64_t *values;
  int i, found = -1;

  if (drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props) < 0) {
    perror("MODE_OBJ_GETPROPERTIES");
    return -1;
  }
  ids = calloc(props.count_props ? props.count_props : 1, sizeof(*ids));
  values = calloc(props.count_props ? props.count_props : 1, sizeof(*values));
  props.props_ptr = (uint64_t)(uintptr_t)ids;
  props.prop_values_ptr = (uint64_t)(uintptr_t)values;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props) < 0) {
    perror("MODE_OBJ_GETPROPERTIES (fill)");
    goto out;
  }

  for (i = 0; i < (int)props.count_props; i++) {
    struct drm_mode_get_property prop = { .prop_id = ids[i] };

    if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) < 0)
      continue;
    if (!strcmp(prop.name, want)) {
      found = (int)ids[i];
      break;
    }
  }

out:
  free(ids);
  free(values);
  return found;
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

int main(int argc, char **argv)
{
  const char *dev = argc > 1 ? argv[1] : "/dev/dri/card0";
  struct drm_mode_card_res res = { 0 };
  struct drm_mode_get_connector conn = { 0 };
  struct drm_mode_get_encoder enc = { 0 };
  struct drm_mode_get_plane_res pres = { 0 };
  struct drm_mode_create_dumb dumb = { 0 };
  struct drm_mode_fb_cmd2 fb = { 0 };
  struct drm_mode_map_dumb map = { 0 };
  struct drm_mode_create_blob blob = { 0 };
  struct drm_mode_atomic req = { 0 };
  struct drm_mode_modeinfo *mode;
  uint32_t *crtc_ids, *conn_ids, *plane_ids = NULL;
  uint32_t connector_id, crtc_id = 0, encoder_id, plane_id;
  uint32_t plane_fb_prop, plane_crtc_prop;
  uint32_t crtc_mode_prop, crtc_active_prop, conn_crtc_prop;
  uint32_t objs[3], counts[3], props[5];
  uint64_t values[5];
  int fd, i;

  fd = open(dev, O_RDWR);
  if (fd < 0) {
    perror(dev);
    return 1;
  }
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
  conn_ids = calloc(res.count_connectors, sizeof(*conn_ids));
  crtc_ids = calloc(res.count_crtcs, sizeof(*crtc_ids));
  res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
  res.crtc_id_ptr = (uint64_t)(uintptr_t)crtc_ids;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    perror("MODE_GETRESOURCES (fill)");
    return 1;
  }
  connector_id = conn_ids[0];

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
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
    perror("MODE_GETCONNECTOR (fill)");
    return 1;
  }
  mode = pick_mode(&conn);
  if (!mode) {
    fprintf(stderr, "No mode on connector %u\n", connector_id);
    return 1;
  }

  /* pick the CRTC through the encoder's possible_crtcs mask */
  encoder_id = ((uint32_t *)conn.encoders_ptr)[0];
  enc.encoder_id = encoder_id;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) {
    perror("MODE_GETENCODER");
    return 1;
  }
  for (i = 0; i < (int)res.count_crtcs; i++) {
    if (enc.possible_crtcs & (1u << i)) {
      crtc_id = crtc_ids[i];
      break;
    }
  }
  if (!crtc_id) {
    fprintf(stderr, "No CRTC compatible with encoder %u\n", encoder_id);
    return 1;
  }

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) {
    perror("MODE_GETPLANERESOURCES");
    return 1;
  }
  plane_ids = calloc(pres.count_planes ? pres.count_planes : 1,
                     sizeof(*plane_ids));
  pres.plane_id_ptr = (uint64_t)(uintptr_t)plane_ids;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0 ||
      !pres.count_planes) {
    perror("MODE_GETPLANERESOURCES (fill)");
    return 1;
  }
  plane_id = plane_ids[0];

  dumb.width = mode->hdisplay;
  dumb.height = mode->vdisplay;
  dumb.bpp = 16;
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
  uint16_t *pixels = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          fd, map.offset);
  if (pixels == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  for (i = 0; i < (int)(dumb.size / 2); i++)
    pixels[i] = 0xf800; /* solid red RGB565 */
  munmap(pixels, dumb.size);

  plane_fb_prop = find_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
  plane_crtc_prop = find_prop_id(fd, plane_id, DRM_MODE_OBJECT_PLANE,
                                 "CRTC_ID");
  crtc_mode_prop = find_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
  crtc_active_prop = find_prop_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC,
                                  "ACTIVE");
  conn_crtc_prop = find_prop_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR,
                                "CRTC_ID");
  if (plane_fb_prop < 0 || plane_crtc_prop < 0 || crtc_mode_prop < 0 ||
      crtc_active_prop < 0 || conn_crtc_prop < 0) {
    fprintf(stderr, "Could not find all required properties\n");
    return 1;
  }

  blob.data = (uint64_t)(uintptr_t)mode;
  blob.length = sizeof(*mode);
  if (drm_ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) < 0) {
    perror("MODE_CREATEPROPBLOB");
    return 1;
  }

  /* objects: plane, crtc, connector */
  objs[0] = plane_id;
  objs[1] = crtc_id;
  objs[2] = connector_id;
  /* plane: CRTC_ID, FB_ID */
  props[0] = plane_crtc_prop;
  values[0] = crtc_id;
  props[1] = plane_fb_prop;
  values[1] = fb.fb_id;
  /* crtc: MODE_ID, ACTIVE */
  props[2] = crtc_mode_prop;
  values[2] = blob.blob_id;
  props[3] = crtc_active_prop;
  values[3] = 1;
  /* connector: CRTC_ID */
  props[4] = conn_crtc_prop;
  values[4] = crtc_id;
  counts[0] = 2;
  counts[1] = 2;
  counts[2] = 1;

  req.count_objs = 3;
  req.objs_ptr = (uint64_t)(uintptr_t)objs;
  req.count_props_ptr = (uint64_t)(uintptr_t)counts;
  req.props_ptr = (uint64_t)(uintptr_t)props;
  req.prop_values_ptr = (uint64_t)(uintptr_t)values;

  req.flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &req) < 0) {
    perror("MODE_ATOMIC (TEST_ONLY)");
    return 1;
  }
  printf("TEST_ONLY commit ok: atomic_check ran, nothing was programmed\n");

  req.flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &req) < 0) {
    perror("MODE_ATOMIC");
    return 1;
  }
  printf("ATOMIC commit ok: plane=%u crtc=%u connector=%u fb=%u mode=%ux%u\n",
         plane_id, crtc_id, connector_id, fb.fb_id, mode->hdisplay,
         mode->vdisplay);
  printf("Check dmesg: atomic_update should dump 0xf800 pixels\n");

  drm_ioctl(fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &blob);
  close(fd);
  return 0;
}
