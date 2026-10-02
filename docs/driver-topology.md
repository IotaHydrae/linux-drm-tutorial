[**English**](driver-topology.md) | [**简体中文**](driver-topology.zh-CN.md)

# Topology of the tutorial driver

> One `drm_device` holds a `mode_config`; four objects are wired into a chain
> **plane → crtc → encoder → connector**, with the modes flowing back along the
> chain for validation. Userspace reaches it through `/dev/dri/card0` or the
> emulated `/dev/fb0`.

## TL;DR

- `mode_config` is the device switchboard: object lists plus the function table
  (`fb_create`, `atomic_check`, `atomic_commit`) every commit goes through.
- The **framebuffer** glues KMS objects to memory: geometry plus `fb->obj[0]`,
  where the pixel bytes live.
- **CRTC → plane** via `crtc->primary`; **encoder → CRTC** via
  `encoder->possible_crtcs`; **connector → encoder** via
  `drm_connector_attach_encoder()`.
- Modes are produced by the connector and validated down the pipeline:
  `drm_mode_validate_driver()` → `_size()` → `_flag()` → `_pipeline()`, ending
  at the driver's `.mode_valid`.

## The object graph

```text
          userspace
   ┌──────────┴───────────┐
 /dev/dri/card0        /dev/fb0
 (modetest, ...)       (tests, fbcon)
   │                       │
   ▼                       ▼
 DRM core (ioctls)    fbdev emulation
   └──────────┬────────────┘
              ▼
        drm_device (drm_tutorial)
              │
   ┌──────────┴───────────┐
   │  mode_config:        │
   │  object lists, funcs │
   └──────────┬───────────┘
              │
   ┌──────┬───┴────┬───────┐
   ▼      ▼        ▼       ▼
 plane ─▶ crtc ─▶ encoder ─▶ connector
   │                               │
   └────────▶ framebuffer ◀────────┘ (modes)
               │
               ▼
          GEM DMA buffer (pixels)
```

- `mode_config` keeps the lists of all objects and the function table
  (`fb_create`, `atomic_check`, `atomic_commit`).
- The **plane** is where frames are presented: it references one framebuffer
  and carries the damage clips.
- The **CRTC** owns the plane (`crtc->primary`) and defines the timing; this
  driver accepts only the fixed 128x160 mode.
- The **encoder** has `possible_crtcs`, saying which CRTC may drive it.
- The **connector** supplies the mode list and links to the encoder.
- The **framebuffer** stores the geometry (width/height/pitch/format) and a
  handle to a GEM object (`fb->obj[0]`), where the pixel bytes live.

## How the four objects are glued together

```text
                    drm_device
                        │
        ┌───────────────┴───────────────┐
   drm_connector                    drm_crtc
        │  attach_encoder               │  crtc->primary = plane
        ▼                               ▼
   drm_encoder ── possible_crtcs ── drm_crtc ── drm_plane
```

- **CRTC → plane**: `drm_crtc_init_with_planes()` sets `crtc->primary`, so
  atomic commits know which plane shows the scanout buffer. Because the plane
  was registered with `possible_crtcs = 0` (`drm_universal_plane_init()` in
  `drm_tutorial_create_plane()`), the CRTC helper fills in
  `plane->possible_crtcs = drm_crtc_mask(crtc)`.
- **Encoder → CRTC**: `encoder->possible_crtcs = drm_crtc_mask(crtc)`.
  `drm_mode_validate_pipeline()` uses this mask to decide whether a chosen
  encoder can feed the chosen CRTC.
- **Connector → encoder**: `drm_connector_attach_encoder()` records the link
  in both objects.
- **Connector → modes**: `fill_modes` (probe helper) calls the connector's
  `.get_modes` to build the mode list; each mode is then validated down the
  pipeline: `drm_mode_validate_driver()` → `drm_mode_validate_size()` →
  `drm_mode_validate_flag()` → `drm_mode_validate_pipeline()`, which walks
  connector → encoder → CRTC and calls `drm_crtc_mode_valid()`. That reaches
  the driver's `.mode_valid = drm_tutorial_crtc_helper_mode_valid`, which
  delegates to `drm_crtc_helper_mode_valid_fixed()` (returns `MODE_OK` for
  128x160, `MODE_ONE_WIDTH` / `MODE_ONE_HEIGHT` / `MODE_ONE_SIZE` otherwise).

## Related

- [kms-objects.md](kms-objects.md) - construction and callbacks of each object
- [module-load-and-probe.md](module-load-and-probe.md) - the code that builds this graph
- [gem-dma-framebuffers.md](gem-dma-framebuffers.md) - what `fb->obj[0]` is
