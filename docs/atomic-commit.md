[**English**](atomic-commit.md) | [**简体中文**](atomic-commit.zh-CN.md)

# The atomic commit machinery and the driver's `atomic_update()`

> `drm_atomic_commit()` runs the check phase (driver `atomic_check` callbacks)
> and then the commit tail, which vmaps the framebuffer, calls the driver's
> `atomic_update()` and unmaps it again. `swap_state()` runs before the commit
> tail, so `plane->state` inside `atomic_update()` is already the new state.

## TL;DR

- Check phase: `drm_atomic_helper_check()` →
  `drm_atomic_helper_check_planes()` → `drm_atomic_helper_check_plane_damage()`
  (blob → `plane_state->damage`), then the driver's plane and CRTC
  `atomic_check` callbacks.
- Commit phase: `drm_atomic_helper_setup_commit()` + `prepare_planes()` runs
  `.begin_fb_access = drm_gem_begin_shadow_fb_access` (vmap), then
  `drm_atomic_helper_swap_state()`, then `drm_atomic_helper_commit_tail()`.
- `drm_atomic_helper_commit_planes()` calls
  `drm_tutorial_plane_helper_atomic_update()`; afterwards `end_fb_access`
  vunmaps the framebuffer.
- Because `swap_state()` already ran, `plane->state` is the *new* state;
  the old state comes from `drm_atomic_get_old_plane_state(state, plane)` -
  exactly what the driver does before merging damage.
- The driver's `atomic_update()` walks `fb->obj[0]` →
  `to_drm_gem_dma_obj(obj)` → `dma_obj->vaddr`, logs geometry and the top-left
  4x4 pixels, then merges and logs the damage rectangle.

## Check and commit call tree

```text
drm_atomic_commit(state)                    [drm_atomic.c]
  ├─ drm_atomic_check_only()
  │    └─ mode_config.funcs->atomic_check = drm_atomic_helper_check
  │         ├─ drm_atomic_helper_check_modeset()
  │         └─ drm_atomic_helper_check_planes()
  │              ├─ drm_atomic_helper_check_plane_damage()
  │              │    → fb_damage_clips blob → plane_state->damage
  │              ├─ plane->helper_private->atomic_check
  │              │    = drm_tutorial_plane_helper_atomic_check
  │              │      └─ drm_atomic_helper_check_plane_state(
  │              │           ..., DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
  │              │           false, false)
  │              └─ crtc->helper_private->atomic_check
  │                   = drm_tutorial_crtc_helper_atomic_check
  │                     ├─ drm_atomic_helper_check_crtc_primary_plane()
  │                     └─ drm_atomic_add_affected_planes()
  └─ mode_config.funcs->atomic_commit = drm_atomic_helper_commit
       ├─ drm_atomic_helper_setup_commit() + prepare_planes()
       │    └─ plane->helper_private->begin_fb_access
       │         = drm_gem_begin_shadow_fb_access()
       │           └─ drm_gem_fb_vmap()   // map the fb's BOs into kernel VA
       ├─ drm_atomic_helper_swap_state()  // old/new states swapped;
       │                                  // plane->state now points at the NEW state
       └─ commit_tail() (blocking path)
            └─ mode_config.helper_private->atomic_commit_tail
                 = drm_atomic_helper_commit_tail()
                   ├─ drm_atomic_helper_commit_modeset_disables()
                   │    → crtc atomic_disable (logs only)
                   ├─ drm_atomic_helper_commit_planes(dev, state, 0)
                   │    └─ for each plane in the state:
                   │         plane->helper_private->atomic_update
                   │           = drm_tutorial_plane_helper_atomic_update()
                   │    └─ then end_fb_access loop:
                   │         drm_gem_end_shadow_fb_access() → drm_gem_fb_vunmap()
                   ├─ drm_atomic_helper_commit_modeset_enables()
                   │    → crtc atomic_enable (logs only)
                   ├─ drm_atomic_helper_fake_vblank()
                   ├─ drm_atomic_helper_commit_hw_done()
                   ├─ drm_atomic_helper_wait_for_vblanks()
                   └─ drm_atomic_helper_cleanup_planes()
                        → plane cleanup_fb
       (on error: drm_atomic_helper_unprepare_planes())
```

`drm_atomic_helper_commit_planes()` runs `atomic_update` for every plane in the
state whose new state has a CRTC (or that is being disabled). The
`begin_fb_access` / `end_fb_access` helpers bracket the update:
`drm_gem_begin_shadow_fb_access()` vmaps the framebuffer objects into kernel
address space, `drm_gem_end_shadow_fb_access()` unmaps them again.

## Inside `drm_tutorial_plane_helper_atomic_update()`

This is the driver's "hardware programming" step - for a real device this is
where you would program scanout registers. The tutorial version inspects the
state instead:

1. `if (!fb) return;` - the plane may be disabled, in which case there is no
   framebuffer.
2. `drm_atomic_get_old_plane_state(state, plane)` - remember the previous
   state; it is needed for damage merging.
3. `drm_dev_enter()` - guard against the device being unplugged concurrently
   (`drm_tutorial_remove()` calls `drm_dev_unplug()`).
4. Walk from the framebuffer to the backing memory: `fb->obj[0]` →
   `to_drm_gem_dma_obj(obj)` → `dma_obj->vaddr`. This is the kernel virtual
   address of the GEM DMA buffer - the same mapping the fbdev client vmap'ed,
   so the pixels written through `/dev/fb0` are visible here.
5. Print the framebuffer geometry (`width`, `height`, `pitches[0]`, `vaddr`)
   and dump the top-left 4x4 RGB565 pixels, indexing with
   `y * (fb->pitches[0] / 2) + x` (pitch is in bytes, RGB565 is 2 bytes).
6. `drm_atomic_helper_damage_merged(old_plane_state, plane_state, &rect)` -
   merge the damage rectangles of the old and new plane state into one
   rectangle, then log `x1,y1,x2,y2`. Merging both states matters because the
   fbdev client may accumulate several dirtyfb clips before the worker runs; a
   region can be damaged in both states and would otherwise be reported twice
   or missed.
7. `drm_dev_exit()` - balance the earlier enter.

## Caveats

- The helper function names above are specific to the WSL2 kernel documented in
  the root README and were not re-verified against another kernel version.

## Related

- [fbdev-write-path.md](fbdev-write-path.md) - what calls `drm_atomic_commit()` here
- [kms-objects.md](kms-objects.md) - the callbacks referenced above
- [driver-faq.md](driver-faq.md) - why the state looks the way it does
