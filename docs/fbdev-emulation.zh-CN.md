[**English**](fbdev-emulation.md) | [**简体中文**](fbdev-emulation.zh-CN.md)

# fbdev 模拟引导：`drm_client_setup()` → `/dev/fb0`

> `drm_client_setup()` 注册一个内核内 fbdev 客户端：它探测 connector、创建 GEM
> framebuffer、用*影子缓冲*加 deferred I/O 包住它，然后调用
> `register_framebuffer()`。`/dev/fb0` 就在这里出现，第一次原子提交也发生在这里。

## TL;DR

- `drm_fbdev_client_setup()` 从 `mode_config.preferred_depth` 读取
  `color_mode`（16 → RGB565）并建立 fb helper。
- `drm_client_modeset_probe()` 执行 `fill_modes()` → `.get_modes` → 校验，然后
  用 `drm_client_pick_crtcs()` 挑一块 CRTC。
- `drm_fb_helper_single_fb_probe()` 调用驱动的 `.fbdev_probe =
  drm_fbdev_dma_driver_fbdev_probe`，它通过 `drm_client_framebuffer_create()`
  创建客户端 framebuffer 并 vmap 它。
- 之所以走 **shadowed** 路径，仅仅因为 `fb->funcs->dirty` 已设置（驱动用了
  `drm_gem_fb_create_with_dirty`）：一块 `vzalloc` 出来的影子缓冲成为
  `info->screen_buffer`，并安装 deferred I/O。
- `register_framebuffer(info)` 创建 `/dev/fb0`；随后 fbcon 的 `fb_set_par` 通过
  `drm_client_modeset_commit()` → `drm_atomic_commit()` 触发第一次 modeset。

## 调用树

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

最后这一调用就是根 README 里 probe 的 dmesg 序列的来源：
`drm_tutorial_crtc_helper_atomic_enable`，然后是重复出现的整屏 damage
`x1:0 y1:0 x2:128 y2:160` 的 `drm_tutorial_plane_helper_atomic_update`，接着是
`Console: switching to colour frame buffer device 16x20`。

只有 framebuffer 是通过 `drm_gem_fb_create_with_dirty` 创建的（即
`fb->funcs->dirty` 已设置）时才会走 *shadowed* fbdev 路径。这正是驱动刻意在
mode config 里注册 `.fb_create = drm_gem_fb_create_with_dirty` 的原因。

## 边界与陷阱

- 上面的调用链符号名与文件名（`drm_fbdev_dma.c`、`drm_fb_helper.c`、
  `clients/drm_fbdev_client.c`……）是针对根 README 所述 WSL2 内核的。它们没有在
  其他内核版本上重新验证；换内核后 shadow 路径可能分散在别的函数名下。

## 相关

- [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) —— `/dev/fb0` 出现之后发生什么
- [module-load-and-probe.zh-CN.md](module-load-and-probe.zh-CN.md) —— 调用它的第 10 步
- [gem-dma-framebuffers.zh-CN.md](gem-dma-framebuffers.zh-CN.md) —— 被创建的 framebuffer
