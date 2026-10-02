[**English**](drm-ioctls.md) | [**简体中文**](drm-ioctls.zh-CN.md)

# DRM ioctls in practice

> Everything the driver does happens behind ioctls. Use `modetest` for a
> no-code poke, or the raw-ioctl programs in `examples/drm/` to see exactly
> which requests reach the driver. Both `SETCRTC` and `MODE_ATOMIC` funnel into
> the same `drm_atomic_commit()` path.

## TL;DR

- `modetest -M drm_tutorial -c/-e/-p` enumerates connectors, encoders and
  planes; `-s`/`-a` set a mode.
- A successful modeset shows `atomic_check` → `atomic_update` →
  `atomic_enable` in dmesg - the same callbacks as fbdev writes.
- `MODE_GETCONNECTOR` goes through `fill_modes` → `.get_modes` → CRTC
  `.mode_valid`.
- `examples/drm/{probe,setcrtc,atomic}.out` use raw `ioctl()` only, no libdrm,
  and need `/usr/include/drm/drm.h` (package: `libdrm-dev`).
- `SETCRTC` and `MODE_ATOMIC` need DRM master, so the examples call
  `DRM_IOCTL_SET_MASTER` (root, no compositor holding the device).

## modetest: poke the driver without writing code

```bash
sudo apt install libdrm-tools   # provides modetest

modetest -M drm_tutorial -c   # connectors and their modes (GETCONNECTOR)
modetest -M drm_tutorial -e   # encoders                  (GETENCODER)
modetest -M drm_tutorial -p   # planes                    (GETPLANERESOURCES / GETPLANE)
modetest -M drm_tutorial -s 32:128x160          # legacy modeset (SETCRTC)
modetest -M drm_tutorial -a -s 32@33:128x160    # atomic modeset (MODE_ATOMIC)
```

The connector and CRTC IDs (here 32 and 33) come from the `-c` output; mode
syntax details are in `modetest -h`. `make test` already uses the `-e` variant.
A successful modeset shows up in dmesg as `atomic_check` → `atomic_update` →
`atomic_enable` - the same callbacks as the fbdev writes.

## The ioctl → driver callback map

| ioctl | kernel handler | reaches the driver via |
| ----- | -------------- | ---------------------- |
| `DRM_IOCTL_MODE_GETRESOURCES` | `drm_mode_getresources` | core object lists (no driver code) |
| `DRM_IOCTL_MODE_GETCONNECTOR` | `drm_mode_getconnector` | `connector->funcs->fill_modes` → `.get_modes` (`drm_connector_helper_get_modes_fixed`) → CRTC `.mode_valid` (`drm_crtc_helper_mode_valid_fixed`) |
| `DRM_IOCTL_MODE_GETENCODER` | `drm_mode_getencoder` | core object metadata |
| `DRM_IOCTL_MODE_GETPLANERESOURCES` / `DRM_IOCTL_MODE_GETPLANE` | `drm_mode_getplane_res` / `drm_mode_getplane` | core object metadata |
| `DRM_IOCTL_MODE_CREATE_DUMB` | `drm_mode_create_dumb_ioctl` | `driver->dumb_create = drm_gem_dma_dumb_create` |
| `DRM_IOCTL_MODE_ADDFB2` | `drm_mode_addfb2_ioctl` | `mode_config.funcs->fb_create = drm_gem_fb_create_with_dirty` |
| `DRM_IOCTL_MODE_MAP_DUMB` | `drm_mode_mmap_dumb` | GEM DMA mmap |
| `DRM_IOCTL_MODE_SETCRTC` | `drm_mode_setcrtc` → `drm_mode_set_config_internal` | `crtc->funcs->set_config = drm_atomic_helper_set_config` → `drm_atomic_commit` → check/commit |
| `DRM_IOCTL_MODE_ATOMIC` | `drm_mode_atomic_ioctl` | `drm_atomic_commit` → same check/commit path |

The last two rows are the punchline: both the legacy `SETCRTC` ioctl and the
modern atomic ioctl funnel into the exact same machinery described in
[atomic-commit.md](atomic-commit.md).

## The example programs

The programs in `examples/drm/` use only the raw `ioctl()` syscall - no libdrm.
They need the kernel UAPI headers (`sudo apt install libdrm-dev` provides
`/usr/include/drm/drm.h`):

```bash
make -C examples/drm
sudo ./examples/drm/probe.out
sudo ./examples/drm/setcrtc.out
sudo ./examples/drm/atomic.out
```

**`probe.out`** performs `GETRESOURCES`, then `GETCONNECTOR` / `GETENCODER` /
`GETPLANE` for every object and prints the IDs and modes. Run it first - the
printed IDs are what the other tools expect. It is also a good way to see that
`GETCONNECTOR` goes through `fill_modes`: with the tutorial driver you get
exactly one 128x160 mode, marked `(preferred)`.

**`setcrtc.out`** drives the legacy path: create a 16 bpp dumb buffer
(`CREATE_DUMB`), attach it as an RGB565 framebuffer (`ADDFB2`), fill it with a
gradient through `MAP_DUMB` + `mmap`, then call `SETCRTC` with the connector and
the mode. In dmesg you should see `atomic_check`, `atomic_update` and
`atomic_enable` - proof that the legacy ioctl is translated into an atomic
commit.

**`atomic.out`** demonstrates the modern API:

1. discover the property IDs (`FB_ID`, `CRTC_ID`, `MODE_ID`, `ACTIVE`) with
   `MODE_OBJ_GETPROPERTIES` + `MODE_GETPROPERTY`;
2. create a mode blob (`CREATEPROPBLOB`) containing the 128x160
   `drm_mode_modeinfo`;
3. submit one atomic state for the plane, CRTC and connector, first with
   `DRM_MODE_ATOMIC_TEST_ONLY` - dmesg shows `atomic_check` only, nothing is
   programmed;
4. then submit the real commit - dmesg now also shows `atomic_update`, and
   because the buffer was filled with `0xf800` (red), the driver's 4x4 pixel
   dump prints `0xf800` values.

## Related

- [atomic-commit.md](atomic-commit.md) - the shared check/commit path
- [driver-faq.md](driver-faq.md) - what `modetest` does step by step
- [module-load-and-probe.md](module-load-and-probe.md) - the callbacks being reached
