[**English**](gem-dma-framebuffers.md) | [**简体中文**](gem-dma-framebuffers.zh-CN.md)

# Framebuffers and GEM DMA memory

> Every framebuffer is created through `drm_gem_fb_create_with_dirty()`, which
> attaches `.dirty = drm_atomic_helper_dirtyfb`. That single hook is why
> damage clips from `/dev/fb0` writes turn into atomic commits.

## TL;DR

- A framebuffer (`struct drm_framebuffer`) describes a 2D buffer: width,
  height, format (RGB565), pitch, modifiers, plus GEM object references
  (`fb->obj[0]` here).
- Both userspace (`MODE_ADDFB2`) and the fbdev client
  (`drm_client_framebuffer_create()`) create it through the same
  `mode_config.funcs->fb_create`.
- The driver sets `.fb_create = drm_gem_fb_create_with_dirty`, so
  `fb->funcs->dirty = drm_atomic_helper_dirtyfb` is set on every framebuffer.
  The fbdev emulation takes the *shadowed* write path only because of this.
- `fb->obj[0]` is always a DMA GEM object here: `to_drm_gem_dma_obj(obj)`
  exposes `dma_addr` (for hardware) and `vaddr` (the kernel CPU mapping).
- Atomic helpers take/release framebuffer references around commits, so a
  framebuffer is never freed while a plane still points at it.

## Creation

All framebuffers go through `mode_config.funcs->fb_create`, set to
`drm_gem_fb_create_with_dirty`:

- userspace: `DRM_IOCTL_MODE_ADDFB2` → `drm_mode_addfb2()` → `fb_create`;
- the fbdev client: `drm_client_framebuffer_create()` → the same `fb_create`
  (this produces the backing store of `/dev/fb0`).

`drm_gem_fb_create_with_dirty()` validates the requested format against the
plane's format list, creates the `drm_framebuffer`, and attaches
`.dirty = drm_atomic_helper_dirtyfb` to it. That hook is what turns damage
clips into atomic commits in the write path; see
[fbdev-write-path.md](fbdev-write-path.md) and
[atomic-commit.md](atomic-commit.md).

## Memory behind it

`fb->obj[0]` is a `struct drm_gem_object`; in this driver it is always a DMA
GEM object:

- `to_drm_gem_dma_obj(obj)` exposes `dma_addr` (for hardware) and `vaddr`
  (the kernel CPU mapping);
- the fbdev client allocates it through `drm_gem_dma_dumb_create` (the
  driver's `.dumb_create`) as a *dumb buffer*: plain CPU-writable memory, no
  GPU involved;
- the pixel format is fixed to RGB565 because
  `mode_config.preferred_depth = 16` and the plane only advertises
  `DRM_FORMAT_RGB565`.

## Lifecycle

The atomic helpers take and release framebuffer references around commits, so
a framebuffer is never freed while a plane still points at it. During a commit
the shadow helpers (`drm_gem_begin_shadow_fb_access` /
`drm_gem_end_shadow_fb_access`) map `fb->obj[0]` into kernel address space and
unmap it again; see [atomic-commit.md](atomic-commit.md).

Kernel files: `drm_framebuffer.c`, `drm_gem_framebuffer_helper.c`,
`drm_gem_dma_helper.c`.

## Related

- [kms-objects.md](kms-objects.md) - the plane that consumes the framebuffer
- [fbdev-emulation.md](fbdev-emulation.md) - allocation of the fbdev backing store
- [fbdev-write-path.md](fbdev-write-path.md) - where the `dirty` hook is called
