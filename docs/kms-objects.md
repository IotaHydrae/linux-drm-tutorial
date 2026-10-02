[**English**](kms-objects.md) | [**简体中文**](kms-objects.zh-CN.md)

# The four KMS objects of `drm.c`

> `drm.c` registers one primary plane, one CRTC, one no-op encoder and one
> virtual connector, and installs atomic-helper callbacks on each. Almost every
> callback either validates state or logs, because there is no real hardware.

## TL;DR

- Plane: RGB565 only, `DRM_PLANE_TYPE_PRIMARY`, *shadow* state helpers, and
  `FB_DAMAGE_CLIPS` enabled via `drm_plane_enable_fb_damage_clips()`.
- CRTC: binds the plane as `crtc->primary`, validates the fixed mode through
  `drm_crtc_helper_mode_valid_fixed()`, and pulls affected planes into the same
  atomic state.
- Encoder: `DRM_MODE_ENCODER_NONE`; its only real content is
  `possible_crtcs = drm_crtc_mask(crtc)`.
- Connector: `DRM_MODE_CONNECTOR_Unknown`; `get_modes` delegates to
  `drm_connector_helper_get_modes_fixed()` and always yields 128x160
  `DRM_MODE_TYPE_PREFERRED`.
- The callback tables below are the ground truth for what the kernel can call;
  they are taken directly from `drm.c`.

## Plane

**Role.** A plane selects one framebuffer and places it on screen. Real drivers
have several planes (primary, cursor, overlay); this driver has exactly one
primary plane: fixed size, RGB565, no scaling.

**In `drm.c`.** `drm_tutorial_create_plane()`:

- `drm_universal_plane_init(dev, plane, 0, ...)` - `possible_crtcs = 0` here;
  the CRTC fills it in later (see [driver-topology.md](driver-topology.md));
- format list `{ DRM_FORMAT_RGB565 }`, modifiers
  `{ DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID }` - only plain linear
  RGB565 framebuffers are accepted;
- type `DRM_PLANE_TYPE_PRIMARY`.

**Callbacks.**

| Table | Entry | Value | Called when |
| ----- | ----- | ----- | ----------- |
| `drm_plane_funcs` | `.reset` | `drm_gem_reset_shadow_plane` | state is (re)initialized |
| | `.atomic_duplicate_state` | `drm_gem_duplicate_shadow_plane_state` | state is copied for a commit |
| | `.atomic_destroy_state` | `drm_gem_destroy_shadow_plane_state` | state is freed |
| | `.update_plane` | `drm_atomic_helper_update_plane` | plane update ioctl |
| | `.disable_plane` | `drm_atomic_helper_disable_plane` | plane is disabled |
| | `.destroy` | `drm_plane_cleanup` | object teardown |
| `drm_plane_helper_funcs` | `.begin_fb_access` | `drm_gem_begin_shadow_fb_access` | before `atomic_update`: vmap the fb |
| | `.end_fb_access` | `drm_gem_end_shadow_fb_access` | after `atomic_update`: vunmap the fb |
| | `.atomic_check` | `drm_tutorial_plane_helper_atomic_check` | atomic check phase |
| | `.atomic_update` | `drm_tutorial_plane_helper_atomic_update` | atomic commit phase |

Because the state callbacks are the *shadow* variants, the plane state is a
`struct drm_shadow_plane_state`: the standard plane state plus `map`/`data`
slots that hold the framebuffer's kernel mapping while a commit is in flight.

**Damage.** `drm_plane_enable_fb_damage_clips()` adds the standard
`FB_DAMAGE_CLIPS` property. `drm_atomic_helper_dirtyfb()` writes the damage
blob into `plane_state->fb_damage_clips`; the check phase converts it into
`plane_state->damage` (see [atomic-commit.md](atomic-commit.md)).

**Neighbours.** plane ↔ CRTC: `crtc->primary` and `plane->possible_crtcs =
drm_crtc_mask(crtc)` (set by `drm_crtc_init_with_planes()`); plane ↔
framebuffer: `plane->state->fb`.

Kernel files: `drm_plane.c`, `drm_gem_atomic_helper.c`.

## CRTC

**Role.** The CRTC owns the timing: it scans the primary plane out line by line
at the fixed 128x160 mode and produces the pixel stream for the encoder. This
driver has no real hardware, so the callbacks mostly validate and log.

**In `drm.c`.** `drm_tutorial_create_crtc()`:

- `drm_crtc_init_with_planes(dev, crtc, &plane, NULL,
  &drm_tutorial_crtc_funcs, NULL)` binds the plane as `crtc->primary`; because
  the plane was created with `possible_crtcs = 0`, the helper fills it in with
  `drm_crtc_mask(crtc)` (`drm_crtc.c`).

**Callbacks.**

| Table | Entry | Value | Called when |
| ----- | ----- | ----- | ----------- |
| `drm_crtc_funcs` | `.reset` | `drm_atomic_helper_crtc_reset` | state is (re)initialized |
| | `.set_config` | `drm_atomic_helper_set_config` | legacy `SETCONFIG` ioctl |
| | `.page_flip` | `drm_atomic_helper_page_flip` | `PAGE_FLIP` ioctl |
| | `.atomic_duplicate_state` / `.atomic_destroy_state` | atomic helpers | state copy/free |
| | `.destroy` | `drm_crtc_cleanup` | object teardown |
| `drm_crtc_helper_funcs` | `.mode_valid` | `drm_tutorial_crtc_helper_mode_valid` | mode validation during `fill_modes` |
| | `.atomic_check` | `drm_tutorial_crtc_helper_atomic_check` | atomic check phase |
| | `.atomic_enable` / `.atomic_disable` | log only | commit tail |

**Check logic.** `drm_tutorial_crtc_helper_atomic_check()` returns early when
the CRTC is disabled; otherwise it verifies that a primary plane is present
(`drm_atomic_helper_check_crtc_primary_plane()`). Either way it ends by calling
`drm_atomic_add_affected_planes()` - any commit touching this CRTC also pulls
its plane into the same atomic state.

**Neighbours.** CRTC → plane (`crtc->primary`), CRTC ← encoder
(`encoder->possible_crtcs`), CRTC ← modes
(`drm_crtc_helper_mode_valid_fixed`).

Kernel files: `drm_crtc.c`, `drm_atomic_helper.c`.

## Encoder

**Role.** The encoder converts the CRTC's pixel stream into the signal format
expected by the connector (LVDS, HDMI TMDS, ...). This tutorial has no real
signal, so it registers `DRM_MODE_ENCODER_NONE` - a pass-through placeholder
that still carries the topology link between CRTC and connector.

**In `drm.c`.** `drm_tutorial_create_encoder()`:

- `drm_encoder_init(dev, encoder, &drm_tutorial_encoder_funcs,
  DRM_MODE_ENCODER_NONE, NULL)`;
- `encoder->possible_crtcs = drm_crtc_mask(crtc)` - the single CRTC may drive
  this encoder.

**Callbacks.** Only `.destroy = drm_encoder_cleanup`; there is no `mode_valid`
and no atomic hook. The encoder exists mainly for topology and validation
(`drm_encoder_mode_valid()` is simply skipped because the hook is NULL).

**Neighbours.** encoder ↔ CRTC (`possible_crtcs`), encoder ↔ connector
(`drm_connector_attach_encoder()` links both ways).

Kernel files: `drm_encoder.c`, `drm_probe_helper.c` (`drm_encoder_mode_valid`).

## Connector

**Role.** The connector represents the physical plug and answers "what modes
can this display show?". Since the tutorial has no real display, the connector
is virtual (`DRM_MODE_CONNECTOR_Unknown`) and always reports the single fixed
128x160 mode.

**In `drm.c`.** `drm_tutorial_create_connector()`:

- `drm_connector_init(dev, connector, &drm_tutorial_connector_funcs,
  DRM_MODE_CONNECTOR_Unknown)`;
- `drm_connector_helper_add()` with `.get_modes =
  drm_tutorial_connector_get_modes`, which delegates to
  `drm_connector_helper_get_modes_fixed()`: duplicate the fixed mode, mark it
  `DRM_MODE_TYPE_PREFERRED`, add it to the probed-mode list;
- `drm_connector_attach_encoder()` - the link to the encoder.

**Callbacks.**

| Table | Entry | Value | Called when |
| ----- | ----- | ----- | ----------- |
| `drm_connector_funcs` | `.fill_modes` | `drm_helper_probe_single_connector_modes` | mode probing (`GETCONNECTOR`, fbdev client) |
| | `.reset` / `.atomic_duplicate_state` / `.atomic_destroy_state` | atomic helpers | connector state |
| | `.destroy` | `drm_connector_cleanup` | object teardown |
| `drm_connector_helper_funcs` | `.get_modes` | `drm_tutorial_connector_get_modes` | populates the probed-mode list |

**Mode flow.** `fill_modes` → `get_modes` →
`__drm_helper_update_and_validate` (validate driver/size/flag/pipeline, with the
CRTC's `mode_valid` at the end of the chain) → the surviving mode lands in
`connector->modes`, where userspace and the fbdev client pick it up.

**Neighbours.** connector ↔ encoder (`attach_encoder`), connector ↔ modes
(`connector->modes` after probing), connector ↔ CRTC indirectly through the
encoder's `possible_crtcs`.

Kernel files: `drm_connector.c`, `drm_probe_helper.c`.

## Related

- [driver-topology.md](driver-topology.md) - how these objects are wired
- [module-load-and-probe.md](module-load-and-probe.md) - the probe function that creates them
- [atomic-commit.md](atomic-commit.md) - when each callback runs
