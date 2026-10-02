[**English**](kernel-source-map.md) | [**简体中文**](kernel-source-map.zh-CN.md)

# Kernel source map

> Where to read the code behind the call chains in this knowledge base. Paths
> are relative to the kernel source tree used to build the module; the
> directory names are specific to the kernel version documented in the root
> README and were not re-verified against another version.

## TL;DR

- fbdev entry points live under `drivers/video/fbdev/core/`.
- fbdev emulation, the fb helper and the client live under `drivers/gpu/drm/`.
- The atomic helpers and the dirtyfb path are in `drm_atomic_helper.c`,
  `drm_damage_helper.c` and `drm_atomic.c`.

## Map

| Topic | File(s) |
| ----- | ------- |
| fbdev write syscall entry | `drivers/video/fbdev/core/fbmem.c`, `fb_sys_fops.c` |
| deferred I/O (mmap path) | `drivers/video/fbdev/core/fb_defio.c`, `include/linux/fb.h` |
| fbdev emulation (shadow buffer, blit) | `drivers/gpu/drm/drm_fbdev_dma.c` |
| fbdev client (hotplug, initial config) | `drivers/gpu/drm/clients/drm_fbdev_client.c`, `drm_client_setup.c` |
| fbdev helper (damage work, probe) | `drivers/gpu/drm/drm_fb_helper.c` |
| client modeset (initial commit) | `drivers/gpu/drm/drm_client_modeset.c` |
| mode probing / validation | `drivers/gpu/drm/drm_probe_helper.c`, `drm_modes.c` |
| dirtyfb → atomic commit | `drivers/gpu/drm/drm_damage_helper.c`, `drm_atomic.c` |
| atomic helpers (check/commit/planes) | `drivers/gpu/drm/drm_atomic_helper.c` |
| shadow plane helpers | `drivers/gpu/drm/drm_gem_atomic_helper.c`, `drm_gem_framebuffer_helper.c` |
| object registration (plane/CRTC/encoder/connector) | `drivers/gpu/drm/drm_plane.c`, `drm_crtc.c`, `drm_encoder.c`, `drm_connector.c` |
| ioctl dispatch table | `drivers/gpu/drm/drm_ioctl.c` |
| atomic ioctl handler, dumb buffers | `drivers/gpu/drm/drm_atomic_uapi.c`, `drm_dumb_buffers.c` |

## Related

- [fbdev-write-path.md](fbdev-write-path.md) - the write chain these files implement
- [atomic-commit.md](atomic-commit.md) - the commit chain
- [kernel-debugging.md](kernel-debugging.md) - resolving panic addresses to these files
