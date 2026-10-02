[**English**](driver-faq.md) | [**简体中文**](driver-faq.zh-CN.md)

# Tutorial driver FAQ

> Short answers to the questions this driver raises most often. Every answer is
> grounded in `drm.c`, the kernel DRM helpers, or the behaviour described in
> [fbdev-write-path.md](fbdev-write-path.md).

## TL;DR

- Writes are not immediately visible: fbdev writes are flushed asynchronously.
- The shadow buffer exists so `/dev/fb0` can be poked at any time without going
  through DRM on every write, and so damage tracking has a stable CPU-accessible
  copy.
- RGB565 comes from `mode_config.preferred_depth = 16`.
- `FB_DAMAGE_CLIPS` is a standard plane property; the check phase copies it into
  `plane_state->damage`, and `atomic_update()` merges old and new damage.
- `modetest` reaches the same `drm_atomic_helper_commit()` path as fbdev writes.

## Why is my write not immediately visible?

There is no real display hardware in this tutorial, so "visible" means "shows
up in the kernel logs". Even on real hardware, fbdev writes are flushed
asynchronously: the damage worker batches them, and deferred I/O waits before
flushing `mmap`'ed pages.

## Why a shadow buffer at all?

Three reasons:

- fbcon and legacy apps may poke `/dev/fb0` memory at any time, and we do not
  want every write to go through DRM;
- the real GEM buffer may be DMA memory that is not safely writable from
  arbitrary contexts;
- damage tracking needs a stable, CPU-accessible copy to diff against.

## Why is the screen RGB565?

The driver sets `mode_config.preferred_depth = 16`; the fbdev client turns that
into `color_mode = 16`, and `drm_driver_legacy_fb_format()` maps 16 bpp to
`DRM_FORMAT_RGB565`. The plane also only advertises `DRM_FORMAT_RGB565`.

## What is `FB_DAMAGE_CLIPS`?

A standard plane property enabled by `drm_plane_enable_fb_damage_clips()`.
`drm_atomic_helper_check_plane_damage()` copies it into `plane_state->damage`
during the check phase, and `drm_atomic_helper_damage_merged()` merges
old/new state damage during `atomic_update()`.

## What would a real driver do in `atomic_update()`?

It would read the framebuffer's GEM DMA address (`dma_obj->dma_addr`) and
program the scanout registers: framebuffer address, pitch, width/height, pixel
format, and handle enable/disable. This tutorial logs the same information
instead.

## Where does `modetest` fit in?

`modetest` opens `/dev/dri/card0` and asks for the connector modes
(`DRM_IOCTL_MODE_GETCONNECTOR`), which runs `fill_modes` → `get_modes` → mode
validation; then it sets a mode, which goes through the very same
`drm_atomic_helper_commit()` path as the fbdev writes.

## Related

- [fbdev-write-path.md](fbdev-write-path.md) - the asynchronous write path
- [atomic-commit.md](atomic-commit.md) - where the damage is merged
- [drm-ioctls.md](drm-ioctls.md) - exercising the driver from userspace
