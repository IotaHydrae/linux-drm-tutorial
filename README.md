# Linux DRM driver tutorial

A minimal atomic KMS driver for Linux, built as an out-of-tree module and
exercised through a framebuffer under WSL2 or QEMU. The driver wires a
fixed-mode CRTC/plane/encoder/connector pipeline into DRM, then exposes it to
userspace via fbdev emulation.

```
               DRM device
                   │
    ┌──────────────┴──────────────┐
    │                             │
Connector                        CRTC
    │                             │
 Encoder                     Primary Plane
                                  │
                                  │
                             Framebuffer
                                  │
                                  │
                               GEM DMA
```

## Environment

|        |                                    |
| ------ | ---------------------------------- |
| Distro | WSL-Ubuntu 26.04                   |
| Kernel | 6.18.40.1-microsoft-standard-WSL2+ |

The WSL2 kernel sources ([WSL2-Linux-Kernel](https://github.com/microsoft/WSL2-Linux-Kernel.git))
are used both for building the module and for resolving kernel panics.

## Getting started

### 1. Configure the kernel

The stock WSL2 kernel disables most DRM support, so it must be rebuilt with a
few options enabled first. The config file used by WSL lives at
`Microsoft/config-wsl` in the WSL2-Linux-Kernel tree, for example
`/home/developer/microsoft/WSL2-Linux-Kernel/Microsoft/config-wsl`.

Enable the following options:

| Option | Why it is needed |
| ------ | ---------------- |
| `CONFIG_DRM=y` | DRM core |
| `CONFIG_DRM_KMS_HELPER=y` | atomic/KMS helpers used by the driver (`drm_atomic_helper_*`, `drm_crtc_helper_mode_valid_fixed`) |
| `CONFIG_DRM_GEM_DMA_HELPER=y` | GEM DMA buffers (`DEFINE_DRM_GEM_DMA_FOPS`, `drm_gem_dma_*`) |
| `CONFIG_DRM_FBDEV_EMULATION=y` | fbdev emulation (`DRM_FBDEV_DMA_DRIVER_OPS`) |
| `CONFIG_DRM_CLIENT_SETUP=y` | `drm_client_setup()` used at probe time (also requires `CONFIG_DRM_CLIENT=y`, `CONFIG_DRM_CLIENT_LIB=y`, and `CONFIG_DRM_CLIENT_DEFAULT="fbdev"`) |
| `CONFIG_FB=y` | framebuffer subsystem |
| `CONFIG_FB_DEVICE=y` | creates the `/dev/fb*` nodes and registers the fb char device (major 29) — **disabled in the stock config, must be enabled** |
| `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y` | auto-creates `/dev/fb0` when fb0 is registered |
| `CONFIG_FRAMEBUFFER_CONSOLE=y` | optional: renders the kernel console (fbcon) on the virtual framebuffer, useful for testing |

> **`CONFIG_FB_DEVICE` is the critical one.** The stock `config-wsl` ships with
> `# CONFIG_FB_DEVICE is not set`. Without it the kernel still registers fb0
> (dmesg shows `[drm] fb0: drm_tutorialdrm frame buffer device` and fbcon can
> use it), but no `/sys/class/graphics/fb0` or `/dev/fb0` node is created and
> the fb char device is not registered — even a manual `mknod /dev/fb0 c 29 0`
> will not help.

### 2. Build the module

```bash
make
```

`make test` builds the module, reloads it and dumps the modeset state:

```bash
make test
```

### 3. Try it out

#### On WSL

```bash
fbgrab -d /dev/fb0 dump.png     # capture a screenshot
cp dump.png /mnt/c/Users/Admin/Downloads

./tools/fbview.py               # live preview of /dev/fb0
```

#### Under QEMU

```bash
sudo apt install qemu-system-x86

cd ~/microsoft/WSL2-Linux-Kernel

# share a folder with the guest
mkdir qemu-share
cp /path/to/drm-tutorial.ko qemu-share/

# download a prebuilt Alpine initramfs
wget https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/initramfs-virt

# boot the kernel together with the initramfs and the shared folder
sudo qemu-system-x86_64 \
  -kernel arch/x86/boot/bzImage \
  -initrd ./initramfs-virt \
  -append "console=ttyS0 root=/dev/ram init=/init" \
  -nographic \
  -virtfs local,path=$PWD/qemu-share,mount_tag=host0,security_model=none,id=host0 \
  -enable-kvm
```

Inside the guest, mount the shared folder and load the module:

```bash
mkdir -p /mnt/host
mount -t 9p -o trans=virtio host0 /mnt/host
cd /mnt/host
insmod drm-tutorial.ko
```

You should see the driver probe and fbcon switch to the virtual framebuffer:

```text
[   43.565783] drm_tutorial: loading out-of-tree module taints kernel.
[   43.567205] drm_tutorial_probe
[   43.568112] [drm] Initialized drm_tutorial 0.1.0 for drm_tutorial on minor 0
[   43.572990] drm_tutorial_plane_helper_atomic_check
[   43.572997] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
[   43.572998] drm_tutorial_crtc_helper_atomic_enable
[   43.573027] drm_tutorial_plane_helper_atomic_check
[   43.573032] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
[   43.573040] Console: switching to colour frame buffer device 16x20
[   43.573042] drm_tutorial_plane_helper_atomic_check
[   43.573043] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
[   43.574150] drm_tutorial_plane_helper_atomic_check
[   43.574154] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
[   43.589147] drm_tutorial drm_tutorial: [drm] fb0: drm_tutorialdrm frame buffer device
[   43.591768] DRM device registered
```

If kernel messages are quiet, raise the console log level:

```bash
echo "7 4 1 7" > /proc/sys/kernel/printk
```

## How it works

The driver is a plain platform driver. On probe it allocates a `drm_device`
and assembles a minimal atomic KMS pipeline from a primary plane, a CRTC, an
encoder and a connector, then asks the kernel's fbdev emulation to expose the
pipeline as `/dev/fb0`. This section is written as a tour:

- sections 0 and 1 build the mental model (what DRM/KMS objects are and how
  they are wired) - start there if DRM is new to you;
- sections 2 and 3 show how the driver registers and how `/dev/fb0` appears;
- section 4 explains, first in plain words and then in exact kernel call
  order, what happens when a user writes pixels to the framebuffer;
- sections 5-7 look at the atomic commit machinery, the driver's
  `atomic_update()` callback, and answer common beginner questions.

All function names can be looked up in the kernel source used to build this
module (`~/microsoft/WSL2-Linux-Kernel`, i.e. the `KDIR` from the Makefile).
The relevant files are listed in section 8.

#### 0. DRM concepts for beginners

**DRM and KMS.** DRM (Direct Rendering Manager) is the kernel subsystem that
owns the display. It has two halves: *KMS* (Kernel Mode Setting), which
describes and programs the display pipeline, and *GEM* (Graphics Execution
Manager), which manages the memory that holds pixels. This tutorial uses the
KMS object model plus the GEM DMA helpers.

**The four KMS objects.** A display pipeline is a chain of objects:

| Object | Role | Everyday analogy | This driver |
| ------ | ---- | ---------------- | ----------- |
| Framebuffer | memory that holds the image data | the film | a GEM DMA buffer |
| Plane | picks a framebuffer and places it on screen | a projector slide | RGB565 primary plane |
| CRTC | scans the plane out at a fixed timing | the projector's clock / scanning head | fixed 128x160 mode |
| Encoder | converts the scanout signal for the connector | the signal converter box | `DRM_MODE_ENCODER_NONE` (no-op) |
| Connector | the plug; exposes the modes the display supports | the socket | virtual connector with one fixed mode |

Pixels flow from the framebuffer up through plane → CRTC → encoder →
connector; modes (resolutions) flow the other way: the connector's `get_modes`
produces them, and the CRTC validates them.

**fbdev and its emulation.** *fbdev* is the legacy Linux framebuffer API
(`/dev/fb0`, `ioctl(FBIOGET_VSCREENINFO)`, `mmap`...). Modern DRM drivers do
not implement fbdev themselves. Instead, DRM ships an *fbdev emulation* layer
that registers a fake `/dev/fb0` in front of the real DRM pipeline: legacy
programs write pixels to it, and the emulation translates those writes into
proper DRM operations internally. That is exactly what this tutorial does:
`tests/fb_fill` and `tests/fb_pixel_set` talk to the old API, while the driver
performs the modern atomic dance underneath.

**Atomic mode setting.** Instead of a pile of legacy ioctls, atomic KMS works
with *state*: build a `drm_atomic_state` describing the desired configuration
(which framebuffer on which plane, which mode on which CRTC...), ask DRM to
*check* it (nothing is applied if the check fails), then *commit* it
(everything is applied at once). Drivers hook into the two phases with
`atomic_check` / `atomic_update` callbacks - exactly the two callbacks
implemented in `drm.c`.

**Two device nodes, one driver.** After loading, you will see both
`/dev/dri/card0` (the modern DRM API used by `modetest`, compositors, ...) and
`/dev/fb0` (the legacy API used by our tests and fbcon). Both end up in the
same driver code paths.

#### 1. The topology of this driver

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

- `mode_config` is the DRM device's switchboard: it keeps the lists of all
  objects and the function table (`fb_create`, `atomic_check`,
  `atomic_commit`) that every commit goes through.
- The **plane** is where frames are presented: it references one framebuffer
  and carries the damage clips.
- The **CRTC** owns the plane (`crtc->primary`) and defines the timing; in
  this driver it only accepts the fixed 128x160 mode.
- The **encoder** has `possible_crtcs`, saying which CRTC may drive it.
- The **connector** supplies the mode list and links to the encoder.
- The **framebuffer** is the glue between KMS objects and memory: it stores
  the geometry (width/height/pitch/format) and a handle to a GEM object
  (`fb->obj[0]`), which is where the actual pixel bytes live.

#### 2. Module load and device registration

```text
insmod drm-tutorial.ko
  └─ module_init(drm_tutorial_init)
       ├─ platform_device_register_simple("drm_tutorial")
       │     → creates the platform device the driver will bind to
       └─ platform_driver_register(&drm_tutorial_platform_driver)
             → driver core matches it against the existing device
             └─ drm_tutorial_probe()
```

`drm_tutorial_probe()` then performs, in order:

1. `drm_dev_alloc(&drm_tutorial_driver, &pdev->dev)` - allocate and initialize
   a `struct drm_device`. The static `drm_tutorial_driver` supplies everything
   the core needs:
   - `.fops = DEFINE_DRM_GEM_DMA_FOPS(drm_tutorial_fops)` - file operations
     for `/dev/dri/card*` (open/release, mmap, ioctl dispatch, DMA-BUF);
   - `.dumb_create = drm_gem_dma_dumb_create` and
     `.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table_vmap` -
     GEM DMA buffer helpers;
   - `.fbdev_probe = drm_fbdev_dma_driver_fbdev_probe` - the fbdev emulation
     entry point used later by `drm_client_setup()`;
   - `DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC` feature bits.
2. Fill in the global `s_drm_disp_mode` with the fixed 128x160 mode
   (`clock = 1`, sync timings equal to the visible size, physical size
   28x35 mm). There is no EDID or hardware probing in this tutorial driver;
   the mode is hard-coded.
3. `drm_mode_config_init()` - initialize the `mode_config` lists, locks and
   object ID allocator.
4. Set the mode config limits: `min/max_width = 128`, `min/max_height = 160`
   and `preferred_depth = 16`. The depth drives the fbdev pixel format later:
   16 bpp means RGB565.
5. `drm->mode_config.funcs = drm_tutorial_mode_config_funcs` - the global
   operation table used by every atomic commit:
   - `.fb_create = drm_gem_fb_create_with_dirty` - every framebuffer created
     through this device gets `.dirty = drm_atomic_helper_dirtyfb` attached to
     it (this hook is the key to the fbdev write path, see step 4 below);
   - `.atomic_check = drm_atomic_helper_check` - generic atomic validation;
   - `.atomic_commit = drm_atomic_helper_commit` - generic atomic commit.
6. `drm->mode_config.helper_private = drm_tutorial_mode_config_helper_funcs`
   with `.atomic_commit_tail = drm_atomic_helper_commit_tail` - the "commit
   tail" that actually programs hardware after a commit is accepted.
7. Create the four KMS objects, in dependency order (see the next subsection
   for how they are linked):
   - `drm_tutorial_create_plane()`:
     - `drm_universal_plane_init()` registers the plane as
       `DRM_PLANE_TYPE_PRIMARY` with the RGB565 format and the LINEAR +
       INVALID modifier list;
     - the plane callbacks are the *shadow plane* helpers:
       `.reset = drm_gem_reset_shadow_plane`,
       `.atomic_duplicate_state = drm_gem_duplicate_shadow_plane_state`,
       `.atomic_destroy_state = drm_gem_destroy_shadow_plane_state` - the
       plane state is therefore a `struct drm_shadow_plane_state` that can
       hold a kernel mapping of the framebuffer;
     - `.update_plane` / `.disable_plane` are the standard atomic helper entry
       points used by the DRM ioctls;
     - `drm_plane_helper_add()` installs `.begin_fb_access =
       drm_gem_begin_shadow_fb_access`, `.end_fb_access =
       drm_gem_end_shadow_fb_access`, and the driver's own
       `.atomic_check` / `.atomic_update`;
     - `drm_plane_enable_fb_damage_clips()` adds the standard
       `FB_DAMAGE_CLIPS` plane property.
   - `drm_tutorial_create_crtc()`:
     - `drm_crtc_init_with_planes(dev, crtc, &plane, NULL, ...)` registers the
       CRTC and stores `crtc->primary = plane` (`drm_crtc.c`), i.e. the plane
       object created in the previous step becomes this CRTC's primary plane;
     - the CRTC callbacks are atomic-helper based (`.set_config =
       drm_atomic_helper_set_config`, `.page_flip =
       drm_atomic_helper_page_flip`, atomic reset/duplicate/destroy);
     - `drm_crtc_helper_add()` installs `.mode_valid =
       drm_tutorial_crtc_helper_mode_valid`, the driver's `.atomic_check`,
       and `.atomic_enable` / `.atomic_disable` (which currently only log).
   - `drm_tutorial_create_encoder()`:
     - `drm_encoder_init(..., DRM_MODE_ENCODER_NONE, NULL)` registers an
       encoder with no real signal encoding;
     - `encoder->possible_crtcs = drm_crtc_mask(crtc)` - a bitmask of the
       CRTCs this encoder can be driven by. It is read by the core whenever a
       mode is validated against the pipeline (see `drm_mode_validate_pipeline`
       below).
   - `drm_tutorial_create_connector()`:
     - `drm_connector_init(..., DRM_MODE_CONNECTOR_Unknown)` registers the
       connector;
     - `drm_connector_helper_add()` installs `.get_modes =
       drm_tutorial_connector_get_modes`, which simply forwards to
       `drm_connector_helper_get_modes_fixed()`: it duplicates the fixed
       128x160 mode, marks it `DRM_MODE_TYPE_PREFERRED` and adds it to the
       connector's probed mode list;
     - `connector->funcs->fill_modes = drm_helper_probe_single_connector_modes`
       is what userspace probing (`DRM_IOCTL_MODE_GETCONNECTOR`) and the
       fbdev client both call to populate `connector->modes`.
   - `drm_connector_attach_encoder(connector, encoder)` - links the connector
     to the encoder (and vice versa via the encoder's connector list).
8. `drm_mode_config_reset()` - allocate the initial state for every object
   (CRTC/plane/connector states are created through the `.reset` callbacks).
9. `drm_dev_register()` - publish the device; this creates the char device
   node (`/dev/dri/card0`) and the sysfs device.
10. `drm_client_setup(drm, NULL)` - set up in-kernel clients, i.e. the fbdev
    emulation that creates `/dev/fb0`. Detailed in step 3 below.

##### How the four objects are glued together

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
  atomic commits know which plane shows the scanout buffer.
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

#### 3. The fbdev emulation bootstrap (`drm_client_setup()` → `/dev/fb0`)

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

This last call is what produces the dmesg sequence shown in the QEMU section
above (`drm_tutorial_crtc_helper_atomic_enable`, then repeated
`drm_tutorial_plane_helper_atomic_update` with the full-screen damage
`x1:0 y1:0 x2:128 y2:160`), followed by
`Console: switching to colour frame buffer device 16x20`.

Note that the *shadowed* fbdev path is taken only because the framebuffer was
created through `drm_gem_fb_create_with_dirty` (i.e. `fb->funcs->dirty` is
set). That is the reason the driver deliberately registers
`.fb_create = drm_gem_fb_create_with_dirty` in its mode config.

#### 4. A user write to `/dev/fb0`, end to end

First, the same journey in plain words. Think of `/dev/fb0` as a notepad
whose real pages live somewhere DRM controls:

1. **The write lands in a scratch copy.** Your bytes are copied into a
   private *shadow buffer* in system memory. This is fast and takes no DRM
   locks, so fbcon can paint at any time without blocking the display
   pipeline. The real pixel memory (the GEM buffer) is not touched yet.
2. **The kernel remembers the dirty rectangle.** The framebuffer core turns
   the byte range you wrote into a rectangle of pixels and merges it into the
   helper's damage clip, then schedules a background worker. Writes are
   *batched*: your `write()` returns immediately; the actual DRM work happens
   later.
3. **The worker copies just the dirty region.** `drm_fbdev_dma_damage_blit`
   copies the clip rectangle from the shadow buffer into the GEM buffer's
   kernel mapping.
4. **A commit is built around the damage.** The worker calls the
   framebuffer's `dirty` hook (`drm_atomic_helper_dirtyfb`), which creates an
   atomic state, attaches the clip as the plane's `FB_DAMAGE_CLIPS` property
   and commits it.
5. **DRM checks, then commits.** The state passes through `atomic_check`
   (your plane/CRTC check callbacks) and then the commit machinery, which
   eventually calls your plane's `atomic_update()` - the driver's moment to
   program the hardware. This tutorial driver only logs what it sees.
6. **Damage from old and new states is merged**, so a rectangle marked dirty
   twice is reported once, covering both.

The exact kernel functions behind each step are in the diagrams below.
This is the path exercised by `tests/fb_fill` and `tests/fb_pixel_set`
(they `mmap()` instead - that variant is shown afterwards):

```text
write(2) /dev/fb0
  → fbmem.c fb_write()
  → info->fbops->fb_write = drm_fbdev_dma_shadowed_defio_write
       [generated by FB_GEN_DEFAULT_DEFERRED_DMAMEM_OPS, include/linux/fb.h]
       ├─ fb_sys_write(): copy_from_user into the shadow buffer
       │    (info->screen_buffer, a vzalloc'ed system-memory copy)
       └─ drm_fb_helper_damage_range(info, offset, ret)
            └─ drm_fb_helper_memory_range_to_clip(): byte range → clip {x1,y1,x2,y2}
            └─ drm_fb_helper_damage():
                 ├─ merge clip into helper->damage_clip (spinlock)
                 └─ schedule_work(&helper->damage_work)

[system workqueue]
drm_fb_helper_damage_work()
  └─ drm_fb_helper_fb_dirty()
       └─ helper->funcs->fb_dirty = drm_fbdev_dma_helper_fb_dirty()
            ├─ drm_fbdev_dma_damage_blit()          [drm_fbdev_dma.c]
            │    └─ drm_fbdev_dma_damage_blit_real()
            │         copy the clip rect from the shadow buffer into the
            │         GEM buffer's kernel mapping (buffer->map, per scanline
            │         using fb->pitches[0])
            └─ helper->fb->funcs->dirty(fb, NULL, 0, 0, clip, 1)
                 = drm_atomic_helper_dirtyfb()      [drm_damage_helper.c]
                      ├─ drm_atomic_state_alloc()
                      ├─ for each plane whose plane->state->fb == fb:
                      │    drm_atomic_get_plane_state()
                      │    drm_property_replace_blob(&plane_state->fb_damage_clips,
                      │                              damage_blob)
                      └─ drm_atomic_commit(state)   → step 4
```

The `mmap()` path used by the tests is similar but goes through deferred I/O:

```text
mmap /dev/fb0 → fb_deferred_io_mmap()  (fb_mmap in the shadowed fbops)
  → page fault → fb_deferred_io_fault()       [fb_defio.c]
      → fb_deferred_io_track_page(): mark page dirty,
        schedule_delayed_work(&info->deferred_work, delay)
          → fb_deferred_io_work()
              → info->fbdefio->deferred_io = drm_fb_helper_deferred_io()
                   → drm_fb_helper_memory_range_to_clip()
                   → drm_fb_helper_damage() → same damage_work as above
```

fbcon rendering takes a third, equivalent route: its `fb_fillrect` /
`fb_copyarea` / `fb_imageblit` map to `drm_fbdev_dma_shadowed_defio_*`, which
draw into the shadow buffer and immediately call
`drm_fb_helper_damage_area()`.

The important trick to understand: **userspace never writes into the GEM
buffer directly**. It writes into a plain `vzalloc`'ed shadow copy; a worker
then accumulates the dirty rectangle and, on flush, blits the affected region
into the real GEM buffer and triggers one atomic commit carrying the damage
clip as the plane's `FB_DAMAGE_CLIPS` property.

#### 5. The atomic commit machinery

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

Two details worth noticing:

- `drm_atomic_helper_commit_planes()` runs `atomic_update` for every plane in
  the state whose new state has a CRTC (or that is being disabled). Because
  `drm_atomic_helper_swap_state()` already ran, inside
  `drm_tutorial_plane_helper_atomic_update()` the expression `plane->state` is
  the *new* state; the *old* state is retrieved with
  `drm_atomic_get_old_plane_state(state, plane)` - exactly what the driver
  does before merging damage.
- The `begin_fb_access` / `end_fb_access` shadow helpers bracket the update:
  `drm_gem_begin_shadow_fb_access()` vmaps the framebuffer objects into kernel
  address space, and `drm_gem_end_shadow_fb_access()` unmaps them again.

#### 6. Inside `drm_tutorial_plane_helper_atomic_update()`

This is the driver's "hardware programming" step - for a real device this is
where you would program scanout registers. The tutorial version inspects the
state instead:

1. `if (!fb) return;` - the plane may be disabled, in which case there is no
   framebuffer.
2. `drm_atomic_get_old_plane_state(state, plane)` - remember the previous
   state; it is needed for damage merging.
3. `drm_dev_enter()` - guard against the device being unplugged concurrently
   (`drm_tutorial_remove()` calls `drm_dev_unplug()`).
4. Walk from the framebuffer to the backing memory:
   `fb->obj[0]` → `to_drm_gem_dma_obj(obj)` → `dma_obj->vaddr`. This is the
   kernel virtual address of the GEM DMA buffer - the same mapping the fbdev
   client vmap'ed in step 2, so the pixels written through `/dev/fb0` are
   visible here.
5. Print the framebuffer geometry (`width`, `height`, `pitches[0]`, `vaddr`)
   and dump the top-left 4x4 RGB565 pixels, indexing with
   `y * (fb->pitches[0] / 2) + x` (pitch is in bytes, RGB565 is 2 bytes).
6. `drm_atomic_helper_damage_merged(old_plane_state, plane_state, &rect)` -
   merge the damage rectangles of the old and new plane state into one
   rectangle, then log `x1,y1,x2,y2`. Merging both states matters because the
   fbdev client may accumulate several dirtyfb clips before the worker runs;
   a region can be damaged in both states and would otherwise be reported
   twice or missed.
7. `drm_dev_exit()` - balance the earlier enter.

#### 7. Questions a beginner might ask

**Why is my write not immediately visible?** There is no real display
hardware in this tutorial, so "visible" means "shows up in the kernel logs".
Even on real hardware, fbdev writes are flushed asynchronously: the damage
worker batches them, and deferred I/O waits ~50 ms (`HZ / 20`) before
flushing mmap'd pages.

**Why a shadow buffer at all?** Three reasons: fbcon and legacy apps may poke
`/dev/fb0` memory at any time and we do not want every write to go through
DRM; the real GEM buffer may be DMA memory that is not safely writable from
arbitrary contexts; and damage tracking needs a stable, CPU-accessible copy
to diff against.

**Why is the screen RGB565?** The driver sets `mode_config.preferred_depth =
16`; the fbdev client turns that into `color_mode = 16`, and
`drm_driver_legacy_fb_format()` maps 16 bpp to `DRM_FORMAT_RGB565`. The plane
also only advertises `DRM_FORMAT_RGB565`.

**What is `FB_DAMAGE_CLIPS`?** A standard plane property enabled by
`drm_plane_enable_fb_damage_clips()`. `drm_atomic_helper_check_plane_damage()`
copies it into `plane_state->damage` during the check phase, and
`drm_atomic_helper_damage_merged()` merges old/new state damage during
`atomic_update()`.

**What would a real driver do in `atomic_update()`?** It would read the
framebuffer's GEM DMA address (`dma_obj->dma_addr`) and program the scanout
registers: framebuffer address, pitch, width/height, pixel format, and handle
enable/disable. This tutorial logs the same information instead.

**Where does `modetest` fit in?** `modetest` opens `/dev/dri/card0` and asks
for the connector modes (`DRM_IOCTL_MODE_GETCONNECTOR`), which runs
`fill_modes` → `get_modes` → mode validation; then it sets a mode, which goes
through the very same `drm_atomic_helper_commit()` path as the fbdev writes.

#### 8. Kernel source map

To follow the call chains in the real kernel source:

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

## Kernel debugging tips

### WSL kernel crash logs

WSL stores crash dumps in `C:\Users\Admin\AppData\Local\Temp\wsl-crashes`;
open the most recent `kernel-panic-xxxxxxxx.txt` file.

### Resolving a panic RIP

Given a RIP line such as `drm_atomic_connector_get_property+0x1a3/0x340`, you
can map it back to a source line either manually or with the bundled scripts.

> You need a `vmlinux` with symbols — build the kernel at least once to
> produce one.

#### Option A: manually

```bash
nm vmlinux | grep drm_atomic_connector_get_property
# ffffffff81d67c10 t drm_atomic_connector_get_property
# ffffffff81d67c00 t __pfx_drm_atomic_connector_get_property

# ffffffff81d67c10 + 0x1a3 = ffffffff81d67db3

addr2line -e vmlinux -i ffffffff81d67db3
# /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808

awk 'NR>=800 && NR<=816 {print NR, $0}' \
  /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c
800             struct drm_property *property, uint64_t *val)
801 {
802     struct drm_device *dev = connector->dev;
803     struct drm_mode_config *config = &dev->mode_config;
804
805     if (property == config->prop_crtc_id) {
806             *val = (state->crtc) ? state->crtc->base.id : 0;
807     } else if (property == config->dpms_property) {
808             if (state->crtc && state->crtc->state->self_refresh_active)
809                     *val = DRM_MODE_DPMS_ON;
810             else
811                     *val = connector->dpms;
812     } else if (property == config->tv_select_subconnector_property) {
813             *val = state->tv.select_subconnector;
814     } else if (property == config->tv_subconnector_property) {
815             *val = state->tv.subconnector;
816     } else if (property == config->tv_left_margin_property) {
```

#### Option B: with the helper scripts

```bash
# resolve the RIP to a file:line
./scripts/ga ~/microsoft/WSL2-Linux-Kernel/vmlinux drm_atomic_connector_get_property+0x1a3
# /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808

# print 8 lines before and after file:line, highlighting the target line
./scripts/pa /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808
```
