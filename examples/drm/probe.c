/*
  drm_probe - enumerate the KMS objects of a DRM device with raw ioctls

  Usage: sudo ./drm_probe.out [/dev/dri/card0]

  Uses DRM_IOCTL_MODE_GETRESOURCES, MODE_GETCONNECTOR, MODE_GETENCODER,
  MODE_GETPLANERESOURCES and MODE_GETPLANE. The printed object IDs are the
  numbers you pass to modetest -s / -a or to drm_setcrtc.out / drm_atomic.out.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

static int drm_ioctl(int fd, unsigned long req, void *arg)
{
  int ret;

  do {
    ret = ioctl(fd, req, arg);
  } while (ret == -1 && errno == EINTR);

  return ret;
}

static void print_mode(const struct drm_mode_modeinfo *m)
{
  printf("      %s %ux%u@%u clock=%u type=0x%x%s\n",
         m->name, m->hdisplay, m->vdisplay, m->vrefresh, m->clock,
         m->type, (m->type & DRM_MODE_TYPE_PREFERRED) ? " (preferred)" : "");
}

static void print_connector(int fd, uint32_t id)
{
  struct drm_mode_get_connector conn = { .connector_id = id };
  uint32_t *encoders = NULL;
  uint32_t *props = NULL;
  uint64_t *prop_values = NULL;
  struct drm_mode_modeinfo *modes = NULL;
  int i;

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
    perror("MODE_GETCONNECTOR");
    return;
  }
  encoders = calloc(conn.count_encoders ? conn.count_encoders : 1,
                    sizeof(*encoders));
  modes = calloc(conn.count_modes ? conn.count_modes : 1, sizeof(*modes));
  props = calloc(conn.count_props ? conn.count_props : 1, sizeof(*props));
  prop_values = calloc(conn.count_props ? conn.count_props : 1,
                       sizeof(*prop_values));
  conn.encoders_ptr = (uint64_t)(uintptr_t)encoders;
  conn.modes_ptr = (uint64_t)(uintptr_t)modes;
  conn.props_ptr = (uint64_t)(uintptr_t)props;
  conn.prop_values_ptr = (uint64_t)(uintptr_t)prop_values;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
    perror("MODE_GETCONNECTOR (fill)");
    goto out;
  }

  printf("Connector %u: type=%u connection=%u encoders:",
         conn.connector_id, conn.connector_type, conn.connection);
  for (i = 0; i < (int)conn.count_encoders; i++)
    printf(" %u", encoders[i]);
  printf(" mode_count=%u prop_count=%u\n", conn.count_modes,
         conn.count_props);
  for (i = 0; i < (int)conn.count_modes; i++)
    print_mode(&modes[i]);

out:
  free(encoders);
  free(modes);
  free(props);
  free(prop_values);
}

static void print_encoder(int fd, uint32_t id)
{
  struct drm_mode_get_encoder enc = { .encoder_id = id };

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) {
    perror("MODE_GETENCODER");
    return;
  }
  printf("Encoder %u: type=%u crtc=%u possible_crtcs=0x%x\n",
         enc.encoder_id, enc.encoder_type, enc.crtc_id, enc.possible_crtcs);
}

static void print_planes(int fd)
{
  struct drm_mode_get_plane_res res = { 0 };
  uint32_t *ids = NULL;
  int j;

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res) < 0) {
    perror("MODE_GETPLANERESOURCES");
    return;
  }
  ids = calloc(res.count_planes ? res.count_planes : 1, sizeof(*ids));
  res.plane_id_ptr = (uint64_t)(uintptr_t)ids;
  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res) < 0) {
    perror("MODE_GETPLANERESOURCES (fill)");
    free(ids);
    return;
  }

  for (j = 0; j < (int)res.count_planes; j++) {
    struct drm_mode_get_plane plane = { .plane_id = ids[j] };
    uint32_t *formats = NULL;
    int k;

    if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) < 0) {
      perror("MODE_GETPLANE");
      continue;
    }
    formats = calloc(plane.count_format_types ? plane.count_format_types : 1,
                     sizeof(*formats));
    plane.format_type_ptr = (uint64_t)(uintptr_t)formats;
    if (drm_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) < 0) {
      perror("MODE_GETPLANE (formats)");
      free(formats);
      continue;
    }

    printf("Plane %u: crtc=%u fb=%u possible_crtcs=0x%x formats:",
           plane.plane_id, plane.crtc_id, plane.fb_id, plane.possible_crtcs);
    for (k = 0; k < (int)plane.count_format_types; k++)
      printf(" 0x%08x", formats[k]);
    printf("\n");
    free(formats);
  }
  free(ids);
}

int main(int argc, char **argv)
{
  const char *dev = argc > 1 ? argv[1] : "/dev/dri/card0";
  struct drm_set_client_cap cap = {
    .capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES,
    .value = 1,
  };
  struct drm_mode_card_res res = { 0 };
  uint32_t *fb_ids, *crtc_ids, *conn_ids, *enc_ids;
  int fd, i;

  fd = open(dev, O_RDWR);
  if (fd < 0) {
    perror(dev);
    return 1;
  }

  /* primary planes are hidden unless this capability is set */
  if (drm_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap) < 0)
    perror("SET_CLIENT_CAP (ignored)");

  if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
    perror("MODE_GETRESOURCES");
    close(fd);
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

  printf("Device %s: %u fbs, %u crtcs, %u connectors, %u encoders\n",
         dev, res.count_fbs, res.count_crtcs, res.count_connectors,
         res.count_encoders);
  printf("Size range: %ux%u .. %ux%u\n", res.min_width, res.min_height,
         res.max_width, res.max_height);

  for (i = 0; i < (int)res.count_connectors; i++)
    print_connector(fd, conn_ids[i]);
  for (i = 0; i < (int)res.count_encoders; i++)
    print_encoder(fd, enc_ids[i]);
  print_planes(fd);

  free(fb_ids);
  free(crtc_ids);
  free(conn_ids);
  free(enc_ids);
  close(fd);
  return 0;
}
