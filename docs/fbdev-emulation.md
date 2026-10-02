[**English**](fbdev-emulation.md) | [**简体中文**](fbdev-emulation.zh-CN.md)

# fbdev emulation bootstrap: `drm_client_setup()` → `/dev/fb0`

> `drm_client_setup()` registers an in-kernel fbdev client that probes the
> connector, creates a GEM framebuffer, wraps it in a *shadow* buffer with
> deferred I/O, and calls `register_framebuffer()`. That is where `/dev/fb0`
> appears and where the first atomic commit happens.

## TL;DR

- `drm_fbdev_client_setup()` reads `color_mode` from
  `mode_config.preferred_depth` (16 → RGB565) and sets up the fb helper.
- `drm_client_modeset_probe()` runs `fill_modes()` → `.get_modes` →
  validation, then picks a CRTC with `drm_client_pick_crtcs()`.
- `drm_fb_helper_single_fb_probe()` calls the driver's `.fbdev_probe =
  drm_fbdev_dma_driver_fbdev_probe`, which creates the client framebuffer
  through `drm_client_framebuffer_create()` and vmaps it.
- The **shadowed** path is taken only because `fb->funcs->dirty` is set (the
  driver uses `drm_gem_fb_create_with_dirty`): a `vzalloc`'ed shadow buffer
  becomes `info->screen_buffer` and deferred I/O is installed.
- `register_framebuffer(info)` creates `/dev/fb0`; fbcon's `fb_set_par` then
  triggers the first modeset through
  `drm_client_modeset_commit()` → `drm_atomic_commit()`.

## Call tree

```text
drm_client_setup(dev, NULL)                     [drm_client_setup.c]
  └─ drm_fbdev_client_setup(dev, NULL)          [clients/drm_fbdev_client.c]
       │  color_mode = mode_config.preferred_depth = 16  → RGB565
       │  kzalloc(drm_fb_helper)
       │  drm_fb_helper_prepare()               [drm_fb_helper.c]
       │     INIT_WORK(damage_work, drm_fb_helper_damage_work)
       │     preferred_bpp = 16
       ├─ drm_client_init(client, "fbdev", &drm_fbdev_client_funcs)
       └─ drm_client_register()
            └─ client->funcs->hotplug = drm_fbdev_client_hotplug
                 ├─ drm_fb_helper_init(dev, fb_helper)
                 └─ drm_fb_helper_initial_config()
                      └─ __drm_fb_helper_initial_config_and_unlock()
                           ├─ drm_client_modeset_probe(client, 128, 160)
                           │    └─ for each connector:
                           │         connector->funcs->fill_modes()
                           │         │   = drm_helper_probe_single_connector_modes [drm_probe_helper.c]
                           │         │     ├─ .get_modes
                           │         │     │    = drm_tutorial_connector_get_modes
                           │         │     │      └─ drm_connector_helper_get_modes_fixed()
                           │         │     │           → 128x160, DRM_MODE_TYPE_PREFERRED
                           │         │     └─ __drm_helper_update_and_validate()
                           │         │          └─ per mode: validate_driver → validate_size
                           │         │             → validate_flag → validate_pipeline
                           │         │                → crtc->helper_private->mode_valid
                           │         │                   = drm_tutorial_crtc_helper_mode_valid
                           │         └─ drm_client_firmware_config() / target_preferred()
                           │            → drm_client_pick_crtcs()   // pick CRTC for the connector
                           │            → store drm_mode_set {crtc, mode, connector}
                           └─ drm_fb_helper_single_fb_probe()
                                ├─ drm_fb_helper_find_sizes()   // 128x160, bpp 16
                                └─ dev->driver->fbdev_probe(fb_helper, &sizes)
                                     = drm_fbdev_dma_driver_fbdev_probe() [drm_fbdev_dma.c]
                                       ├─ drm_client_framebuffer_create()
                                       │    └─ drm_gem_dma_create() + drm_mode_addfb2()
                                       │         → .fb_create = drm_gem_fb_create_with_dirty
                                       │           so fb->funcs->dirty = drm_atomic_helper_dirtyfb
                                       ├─ drm_client_buffer_vmap()  → dma_obj->vaddr
                                       ├─ drm_fb_helper_alloc_info() + drm_fb_helper_fill_info()
                                       └─ fb->funcs->dirty is set → *shadowed* path:
                                            vzalloc(shadow);  info->screen_buffer = shadow
                                            fbops = drm_fbdev_dma_shadowed_fb_ops
                                            fbdefio.deferred_io = drm_fb_helper_deferred_io
                                            fb_deferred_io_init(info)
                                       └─ (back in initial_config) register_framebuffer(info)
                                            → /dev/fb0 appears
                                            → fbcon takes over the console
                                              └─ fb_set_par → drm_fb_helper_set_par()
                                                   └─ __drm_fb_helper_restore_fbdev_mode_unlocked()
                                                        └─ drm_client_modeset_commit()
                                                             └─ drm_client_modeset_commit_atomic()
                                                                  └─ __drm_atomic_helper_set_config()
                                                                       └─ drm_atomic_commit()
                                                                            → first atomic_update()
```

This last call is what produces the probe dmesg sequence in the root README:
`drm_tutorial_crtc_helper_atomic_enable`, then repeated
`drm_tutorial_plane_helper_atomic_update` with the full-screen damage
`x1:0 y1:0 x2:128 y2:160`, followed by
`Console: switching to colour frame buffer device 16x20`.

The *shadowed* fbdev path is taken only because the framebuffer was created
through `drm_gem_fb_create_with_dirty` (i.e. `fb->funcs->dirty` is set). That
is why the driver deliberately registers
`.fb_create = drm_gem_fb_create_with_dirty` in its mode config.

## Caveats

- The call-chain symbol and file names above (`drm_fbdev_dma.c`,
  `drm_fb_helper.c`, `clients/drm_fbdev_client.c`, ...) are specific to the
  WSL2 kernel documented in the root README. They were not re-verified against
  another kernel version; on a different kernel the shadow path may be split
  across differently named functions.

## Related

- [fbdev-write-path.md](fbdev-write-path.md) - what happens after `/dev/fb0` exists
- [module-load-and-probe.md](module-load-and-probe.md) - step 10 that calls this
- [gem-dma-framebuffers.md](gem-dma-framebuffers.md) - the framebuffer being created
