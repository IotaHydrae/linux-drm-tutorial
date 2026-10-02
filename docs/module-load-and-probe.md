[**English**](module-load-and-probe.md) | [**简体中文**](module-load-and-probe.zh-CN.md)

# Module load and device registration

> `insmod` registers a platform device and driver; the driver core matches them
> and calls `drm_tutorial_probe()`, which allocates the `drm_device`, builds
> the KMS objects, registers the DRM device and finally sets up fbdev.

## TL;DR

- The module is a plain platform driver: `platform_device_register_simple()`
  plus `platform_driver_register()`, matched by the name `"drm_tutorial"`.
- `drm_tutorial_probe()` runs in a fixed order: allocate → mode config →
  create objects → attach → reset → `drm_dev_register()` →
  `drm_client_setup()`.
- The mode is hard-coded (no EDID, no probing): 128x160, `clock = 1`,
  physical 28x35 mm.
- `preferred_depth = 16` is what makes the fbdev pixel format RGB565.
- `mode_config.funcs` (`fb_create`, `atomic_check`, `atomic_commit`) and
  `helper_private->atomic_commit_tail` are the tables every commit goes
  through.

## From `insmod` to `probe()`

```text
insmod drm-tutorial.ko
  └─ module_init(drm_tutorial_init)
       ├─ platform_device_register_simple("drm_tutorial")
       │     → creates the platform device the driver will bind to
       └─ platform_driver_register(&drm_tutorial_platform_driver)
             → driver core matches it against the existing device
             └─ drm_tutorial_probe()
```

## What `drm_tutorial_probe()` does, in order

1. `drm_dev_alloc(&drm_tutorial_driver, &pdev->dev)` - allocate and initialize
   a `struct drm_device`. The static `drm_tutorial_driver` supplies everything
   the core needs:
   - `.fops = DEFINE_DRM_GEM_DMA_FOPS(drm_tutorial_fops)` - file operations
     for `/dev/dri/card*` (open/release, mmap, ioctl dispatch, DMA-BUF);
   - `.dumb_create = drm_gem_dma_dumb_create` and
     `.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table_vmap` -
     GEM DMA buffer helpers;
   - `.fbdev_probe = drm_fbdev_dma_driver_fbdev_probe` - the fbdev emulation
     entry point used later by `drm_client_setup()` (it is compiled to `NULL`
     when `CONFIG_DRM_FBDEV_EMULATION` is disabled);
   - `DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC` feature bits.
2. Fill in the global `s_drm_disp_mode` with the fixed 128x160 mode
   (`clock = 1`, sync timings equal to the visible size, physical size
   28x35 mm). There is no EDID or hardware probing; the mode is hard-coded.
3. `drm_mode_config_init()` - initialize the `mode_config` lists, locks and
   object ID allocator.
4. Set the mode config limits: `min/max_width = 128`, `min/max_height = 160`
   and `preferred_depth = 16`. The depth drives the fbdev pixel format later:
   16 bpp means RGB565.
5. `drm->mode_config.funcs = drm_tutorial_mode_config_funcs` - the global
   operation table used by every atomic commit:
   - `.fb_create = drm_gem_fb_create_with_dirty` - every framebuffer created
     through this device gets `.dirty = drm_atomic_helper_dirtyfb` attached
     (the key to the fbdev write path);
   - `.atomic_check = drm_atomic_helper_check` - generic atomic validation;
   - `.atomic_commit = drm_atomic_helper_commit` - generic atomic commit.
6. `drm->mode_config.helper_private = drm_tutorial_mode_config_helper_funcs`
   with `.atomic_commit_tail = drm_atomic_helper_commit_tail` - the "commit
   tail" that actually programs hardware after a commit is accepted.
7. Create the four KMS objects, in dependency order (details in
   [kms-objects.md](kms-objects.md)):
   - `drm_tutorial_create_plane()` - `drm_universal_plane_init()` with
     `DRM_PLANE_TYPE_PRIMARY`, RGB565 and the LINEAR + INVALID modifier list;
     then `drm_plane_helper_add()` with the shadow-plane callbacks and
     `drm_plane_enable_fb_damage_clips()`;
   - `drm_tutorial_create_crtc()` - `drm_crtc_init_with_planes(dev, crtc,
     &plane, NULL, ...)` stores `crtc->primary = plane`; then
     `drm_crtc_helper_add()` installs `.mode_valid`,
     `.atomic_check` and the logging `.atomic_enable` / `.atomic_disable`;
   - `drm_tutorial_create_encoder()` - `drm_encoder_init(...,
     DRM_MODE_ENCODER_NONE, NULL)` and
     `encoder->possible_crtcs = drm_crtc_mask(crtc)`;
   - `drm_tutorial_create_connector()` - `drm_connector_init(...,
     DRM_MODE_CONNECTOR_Unknown)`, `drm_connector_helper_add()` with
     `.get_modes`, and `connector->funcs->fill_modes =
     drm_helper_probe_single_connector_modes`;
   - `drm_connector_attach_encoder(connector, encoder)` - links the connector
     to the encoder (and vice versa via the encoder's connector list).
8. `drm_mode_config_reset()` - allocate the initial state for every object
   (CRTC/plane/connector states are created through the `.reset` callbacks).
9. `drm_dev_register()` - publish the device; this creates the char device
   node (`/dev/dri/card0`) and the sysfs device.
10. `drm_client_setup(drm, NULL)` - set up in-kernel clients, i.e. the fbdev
    emulation that creates `/dev/fb0`; see
    [fbdev-emulation.md](fbdev-emulation.md).

## Related

- [driver-topology.md](driver-topology.md) - the resulting object graph
- [kms-objects.md](kms-objects.md) - per-object callback tables
- [fbdev-emulation.md](fbdev-emulation.md) - step 10 in detail
