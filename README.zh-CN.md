[**English**](README.md) | [**简体中文**](README.zh-CN.md)

# Linux DRM 驱动教程

> 一个为 Linux 编写的最小原子 KMS 驱动，以 out-of-tree 模块构建，在 WSL2 或
> QEMU 下运行：从用户态写一个像素，就能在内核日志里看到整条 DRM 数据链路，直到
> `drm_tutorial_plane_helper_atomic_update()`。

```
          像素扫描输出路径（左 → 右）

┌────────────┐──▶┌────────────┐──▶┌────────────┐──▶┌────────────┐──▶┌────────────┐──▶┌────────────┐
│  GEM DMA   │   │ framebuffer│   │   plane    │   │    CRTC    │   │  encoder   │   │ connector  │
│   pixels   │   │   RGB565   │   │ (primary)  │   │  128x160   │   │   no-op    │   │  128x160   │
└────────────┘   └────────────┘   └────────────┘   └────────────┘   └────────────┘   └────────────┘
```

模式反向流动：connector 的 `get_modes` 产生 128x160 模式，CRTC 负责校验
（`drm_crtc_helper_mode_valid_fixed`）。

## TL;DR

- 一个 C 文件（`drm.c`）注册一块主平面、一块 CRTC、一个空操作 encoder 和一个
  虚拟 connector，固定 **128x160 RGB565** 模式，再通过 fbdev 模拟
  （`drm_client_setup()`）把流水线暴露成 `/dev/fb0`。
- 往 `/dev/fb0` 的写入落在影子缓冲里，一个工作队列把脏矩形 blit 进 GEM DMA 缓冲，
  `drm_atomic_helper_dirtyfb()` 构造原子提交，驱动的 `atomic_update()` 打印
  damage 矩形和左上角 4x4 像素。
- 设计细节都在 [docs/](docs/README.zh-CN.md) 里；README 只讲编译和运行。
- 离线检查（无需硬件）：`make check`。硬件测试需要已加载模块且存在 `/dev/fb0`，
  否则报 `ENVIRONMENT_ERROR`。

## 快速开始

### 环境

|        |                                    |
| ------ | ---------------------------------- |
| 发行版 | WSL-Ubuntu 26.04                   |
| 内核   | 6.18.40.1-microsoft-standard-WSL2+ |

这是驱动开发所用、也是下面示例日志采集所用的环境。WSL2 内核源码
（[WSL2-Linux-Kernel](https://github.com/microsoft/WSL2-Linux-Kernel.git)）既用于
编译本模块，也用于排查内核 panic；下文用 `<kernel-src>` 指代。

### 1. 配置内核

WSL2 自带的内核禁用了大部分 DRM 支持，因此需要先启用若干选项并重新编译内核。
WSL 使用的内核配置文件位于 WSL2-Linux-Kernel 源码树的 `Microsoft/config-wsl`，
即 `<kernel-src>/Microsoft/config-wsl`。

需要启用以下选项：

| 选项 | 作用 |
| ---- | ---- |
| `CONFIG_DRM=y` | DRM 核心 |
| `CONFIG_DRM_KMS_HELPER=y` | 驱动使用的原子/KMS 辅助函数（`drm_atomic_helper_*`、`drm_crtc_helper_mode_valid_fixed`） |
| `CONFIG_DRM_GEM_DMA_HELPER=y` | GEM DMA 缓冲（`DEFINE_DRM_GEM_DMA_FOPS`、`drm_gem_dma_*`） |
| `CONFIG_DRM_FBDEV_EMULATION=y` | fbdev 模拟（`DRM_FBDEV_DMA_DRIVER_OPS`） |
| `CONFIG_DRM_CLIENT_SETUP=y` | probe 阶段使用的 `drm_client_setup()`（还需要 `CONFIG_DRM_CLIENT=y`、`CONFIG_DRM_CLIENT_LIB=y` 和 `CONFIG_DRM_CLIENT_DEFAULT="fbdev"`） |
| `CONFIG_FB=y` | framebuffer 子系统 |
| `CONFIG_FB_DEVICE=y` | 创建 `/dev/fb*` 节点并注册 fb 字符设备（主设备号 29）—— **出厂配置里是关闭的，必须启用** |
| `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y` | fb0 注册时自动创建 `/dev/fb0` |
| `CONFIG_FRAMEBUFFER_CONSOLE=y` | 可选：在虚拟 framebuffer 上渲染内核控制台（fbcon），便于测试 |

> **`CONFIG_FB_DEVICE` 是最关键的一项。** 出厂 `config-wsl` 里是
> `# CONFIG_FB_DEVICE is not set`。没有它，内核仍然会注册 fb0（dmesg 里能看到
> `[drm] fb0: drm_tutorialdrm frame buffer device`，fbcon 也能用），但不会创建
> `/sys/class/graphics/fb0` 和 `/dev/fb0`，fb 字符设备也不会注册——即使手动
> `mknod /dev/fb0 c 29 0` 也没用。*（在以上环境验证。）*

### 2. 编译模块

```bash
make                 # builds drm-tutorial.ko against KDIR
make -C tests        # builds the framebuffer tests
make -C examples/drm # builds the raw DRM-ioctl demos
make check           # offline checks, no module or hardware required
```

`KDIR` 默认是 `/lib/modules/$(uname -r)/build`，可用
`make KDIR=/path/to/kernel/build` 覆盖。

`make test` 会编译模块、用 `rmmod`/`insmod` 重新加载，并用 `modetest -e` 打印
modeset 状态。它会改动正在运行的内核，不要在你不准备改动的机器上运行。

### 3. 尝试运行

#### 在 WSL 中

```bash
fbgrab -d /dev/fb0 dump.png     # 截图
cp dump.png <windows-share>

./tools/fbview.py               # 实时预览 /dev/fb0
```

示例效果——fbcon 控制台文字渲染在虚拟 framebuffer 上：

![虚拟 framebuffer 截图](assets/dump.png)

#### 在 QEMU 中

```bash
sudo apt install qemu-system-x86

cd <kernel-src>

# 创建与 guest 共享的目录
mkdir qemu-share
cp /path/to/drm-tutorial.ko qemu-share/

# 下载预编译的 Alpine initramfs
wget https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/initramfs-virt

# 用 initramfs 和共享目录一起启动内核
sudo qemu-system-x86_64 \
  -kernel arch/x86/boot/bzImage \
  -initrd ./initramfs-virt \
  -append "console=ttyS0 root=/dev/ram init=/init" \
  -nographic \
  -virtfs local,path=$PWD/qemu-share,mount_tag=host0,security_model=none,id=host0 \
  -enable-kvm
```

进入 guest 后，挂载共享目录并加载模块：

```bash
mkdir -p /mnt/host
mount -t 9p -o trans=virtio host0 /mnt/host
cd /mnt/host
insmod drm-tutorial.ko
```

你应该能看到驱动 probe 以及 fbcon 切换到虚拟 framebuffer。下面这段日志是在以上
环境上的一次观察；时间戳和部分顺序会随运行变化。

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

如果内核日志太少，调高控制台日志级别：

```bash
echo "7 4 1 7" > /proc/sys/kernel/printk
```

## 文档

设计文档索引在 [docs/README.zh-CN.md](docs/README.zh-CN.md)。速查表：

| 文档 | 内容 |
| ---- | ---- |
| [drm-kms-concepts.zh-CN.md](docs/drm-kms-concepts.zh-CN.md) | DRM/KMS/GEM 词汇与原子模式设置 |
| [driver-topology.zh-CN.md](docs/driver-topology.zh-CN.md) | 本驱动构建的对象图 |
| [kms-objects.zh-CN.md](docs/kms-objects.zh-CN.md) | plane/CRTC/encoder/connector 回调 |
| [gem-dma-framebuffers.zh-CN.md](docs/gem-dma-framebuffers.zh-CN.md) | framebuffer、GEM DMA 内存、`dirty` 钩子 |
| [module-load-and-probe.zh-CN.md](docs/module-load-and-probe.zh-CN.md) | `probe()` 逐步说明 |
| [fbdev-emulation.zh-CN.md](docs/fbdev-emulation.zh-CN.md) | `drm_client_setup()` → `/dev/fb0` |
| [fbdev-write-path.zh-CN.md](docs/fbdev-write-path.zh-CN.md) | 一次 `/dev/fb0` 写入，端到端 |
| [atomic-commit.zh-CN.md](docs/atomic-commit.zh-CN.md) | 提交机制与 `atomic_update()` |
| [driver-faq.zh-CN.md](docs/driver-faq.zh-CN.md) | 常见问题 |
| [drm-ioctls.zh-CN.md](docs/drm-ioctls.zh-CN.md) | `modetest`、ioctl 对照表、示例程序 |
| [kernel-source-map.zh-CN.md](docs/kernel-source-map.zh-CN.md) | 哪个内核文件实现什么 |
| [kernel-debugging.zh-CN.md](docs/kernel-debugging.zh-CN.md) | 解析 panic RIP |

每篇文档都有英文原件（`<name>.md`）；两者必须内容对等。知识库约定见工作区
[AGENTS.md](../AGENTS.md)。

## 许可证

GPL-2.0；见 [LICENSE](LICENSE)。
