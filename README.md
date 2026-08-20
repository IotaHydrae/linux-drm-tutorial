# Linux DRM driver tutorial

This is a Linux DRM driver tutorial project.

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

## System Info

|        |                                    |
| ------ | ---------------------------------- |
| Distro | WSL-Ubuntu 26.04                   |
| Kernel | 6.18.40.1-microsoft-standard-WSL2+ |

Kernel source: [https://github.com/microsoft/WSL2-Linux-Kernel.git](https://github.com/microsoft/WSL2-Linux-Kernel.git)

## Get-Started

### Configure the kernel

The stock WSL2 kernel disables most DRM support. The config file used to build
the WSL kernel is `Microsoft/config-wsl` in the WSL2-Linux-Kernel source tree
(e.g. `/home/developer/microsoft/WSL2-Linux-Kernel/Microsoft/config-wsl`).

The following options must be enabled for this driver to build and load:

| Option                                          | Why it is needed                                                                                                                           |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `CONFIG_DRM=y`                                  | DRM core                                                                                                                                   |
| `CONFIG_DRM_KMS_HELPER=y`                       | `drm_simple_display_pipe`, atomic/KMS helpers                                                                                              |
| `CONFIG_DRM_GEM_DMA_HELPER=y`                   | GEM DMA buffers (`DEFINE_DRM_GEM_DMA_FOPS`, `drm_gem_dma_*`)                                                                               |
| `CONFIG_DRM_FBDEV_EMULATION=y`                  | fbdev emulation (`DRM_FBDEV_DMA_DRIVER_OPS`)                                                                                               |
| `CONFIG_DRM_CLIENT_SETUP=y`                     | `drm_client_setup()` used by the driver (also needs `CONFIG_DRM_CLIENT=y`, `CONFIG_DRM_CLIENT_LIB=y`, `CONFIG_DRM_CLIENT_DEFAULT="fbdev"`) |
| `CONFIG_FB=y`                                   | framebuffer subsystem                                                                                                                      |
| `CONFIG_FB_DEVICE=y`                            | creates the `/dev/fb*` device nodes and registers the fb char device (major 29)                                                            | **disabled — must enable** |
| `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y` | auto-create `/dev/fb0` when fb0 is registered                                                                                              |
| `CONFIG_FRAMEBUFFER_CONSOLE=y`                  | optional: fbcon console on the dummy fb, useful for testing                                                                                |

> **`CONFIG_FB_DEVICE` is the critical one.** The default `config-wsl` has
> `# CONFIG_FB_DEVICE is not set`. Without it the kernel still registers fb0
> (dmesg shows `[drm] fb0: drm_tutorialdrm frame buffer device` and fbcon can use it),
> but no `/sys/class/graphics/fb0` and no `/dev/fb0` are created, and the fb
> char device is not registered, so a manual `mknod /dev/fb0 c 29 0` will not
> work either.

### Build and Test

#### Test it in your WSL

```bash
make test

fbgrab -d /dev/fb0 dump.png
cp dump.png /mnt/c/Users/Admin/Downloads

./tools/fbview.py
```

#### Test it by qemu

```bash
sudo apt install qemu-system-x86

cd ~/microsoft/WSL2-Linux-Kenrel

# create share folder for qemu
mkdir qemu-share

cp /path/to/drm-tutorial.ko qemu-share/

# download a prebuilt rootfs img
wget https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/initramfs-virt

# start qemu with spec kernel,rootfs,cmdline...
sudo qemu-system-x86_64 -kernel arch/x86/boot/bzImage -initrd ./initramfs-virt -append "console=ttyS0 root=/dev/ram init=/init" -nographic -virtfs local,path=$PWD/qemu-share,mount_tag=host0,security_model=none,id=host0 -enable-kvm

# in qemu
mkdir -p /mnt/host
mount -t 9p -o trans=virtio host0 /mnt/host
cd /mnt/host

insmod drm-tutorial.ko
# [   43.565783] drm_tutorial: loading out-of-tree module taints kernel.
# [   43.567205] drm_tutorial_probe
# [   43.568112] [drm] Initialized drm_tutorial 0.1.0 for drm_tutorial on minor 0
# [   43.572990] drm_tutorial_plane_helper_atomic_check
# [   43.572997] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
# [   43.572998] drm_tutorial_crtc_helper_atomic_enable
# [   43.573027] drm_tutorial_plane_helper_atomic_check
# [   43.573032] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
# [   43.573040] Console: switching to colour frame buffer device 16x20
# [   43.573042] drm_tutorial_plane_helper_atomic_check
# [   43.573043] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
# [   43.574150] drm_tutorial_plane_helper_atomic_check
# [   43.574154] drm_tutorial_plane_helper_atomic_update, x1 : 0, y1 : 0, x2 : 128, y2 : 160
# [   43.589147] drm_tutorial drm_tutorial: [drm] fb0: drm_tutorialdrm frame buffer device
# [   43.591768] DRM device registered

# you may want to change the kernel console log level
echo "4 4 1 7" > /proc/sys/kernel/printk
```

### How it works

TODO

## Kernel Debug Tips

### WSL kernel crash logs

```shell
C:\Users\Admin\AppData\Local\Temp\wsl-crashes
```

find the lastest `kernel-panic-xxxxxxxx.txt` file and open it.

### kernel panic RIP info backtrace

- example 1, raw cmd version

> To get the `vmlinux` you need to compile the kernel at least once

```bash
[ 1966.394761] RIP: 0010:drm_atomic_connector_get_property+0x1a3/0x340

❯ nm vmlinux | grep drm_atomic_connector_get_property
ffffffff81d67c10 t drm_atomic_connector_get_property
ffffffff81d67c00 t __pfx_drm_atomic_connector_get_property

ffffffff81d67c10 + 0x1a3 = FFFFFFFF81D67DB3

❯ addr2line -e vmlinux -i FFFFFFFF81D67DB3
/home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808

❯ awk 'NR>=800 && NR<=816 {print NR, $0}' /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c
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

- example 2, use scripts

```bash
[  183.411864] RIP: 0010:drm_mode_validate_driver+0x86/0xd0

❯ ./scripts/ga ~/microsoft/WSL2-Linux-Kernel/vmlinux drm_atomic_connector_get_property+0x1a3
/home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808

# will highlight the line 808
❯ ./scripts/pa /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808
800 		struct drm_property *property, uint64_t *val)
801 {
802 	struct drm_device *dev = connector->dev;
803 	struct drm_mode_config *config = &dev->mode_config;
804
805 	if (property == config->prop_crtc_id) {
806 		*val = (state->crtc) ? state->crtc->base.id : 0;
807 	} else if (property == config->dpms_property) {
808 		if (state->crtc && state->crtc->state->self_refresh_active)
809 			*val = DRM_MODE_DPMS_ON;
810 		else
811 			*val = connector->dpms;
812 	} else if (property == config->tv_select_subconnector_property) {
813 		*val = state->tv.select_subconnector;
814 	} else if (property == config->tv_subconnector_property) {
815 		*val = state->tv.subconnector;
816 	} else if (property == config->tv_left_margin_property) {
```
