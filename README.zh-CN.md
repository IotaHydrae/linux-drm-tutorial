[**English**](README.md) | [**简体中文**](README.zh-CN.md)

# Linux DRM 驱动教程

一个为 Linux 编写的最小原子 KMS 驱动，以 out-of-tree 模块的形式构建，并通过
WSL2 或 QEMU 下的 framebuffer 实际运行验证。驱动把一条固定模式的
CRTC/plane/encoder/connector 流水线接入 DRM，再通过 fbdev 模拟把显示输出暴露
给用户态。

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

## 环境

|        |                                    |
| ------ | ---------------------------------- |
| 发行版 | WSL-Ubuntu 26.04                   |
| 内核   | 6.18.40.1-microsoft-standard-WSL2+ |

WSL2 内核源码（[WSL2-Linux-Kernel](https://github.com/microsoft/WSL2-Linux-Kernel.git)）
既用于编译本模块，也用于排查内核 panic。

## 快速开始

### 1. 配置内核

WSL2 自带的内核禁用了大部分 DRM 支持，因此需要先启用若干选项并重新编译内核。
WSL 使用的内核配置文件位于 WSL2-Linux-Kernel 源码树的 `Microsoft/config-wsl`，
例如 `/home/developer/microsoft/WSL2-Linux-Kernel/Microsoft/config-wsl`。

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
> `mknod /dev/fb0 c 29 0` 也没用。

### 2. 编译模块

```bash
make
```

`make test` 会编译模块、重新加载并打印 modeset 状态：

```bash
make test
```

### 3. 尝试运行

#### 在 WSL 中

```bash
fbgrab -d /dev/fb0 dump.png     # 截图
cp dump.png /mnt/c/Users/Admin/Downloads

./tools/fbview.py               # 实时预览 /dev/fb0
```

#### 在 QEMU 中

```bash
sudo apt install qemu-system-x86

cd ~/microsoft/WSL2-Linux-Kernel

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

你应该能看到驱动 probe 以及 fbcon 切换到虚拟 framebuffer：

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

## 工作原理

本驱动是一个普通的平台驱动。probe 时它会分配一个 `drm_device`，用主平面
（primary plane）、CRTC、encoder 和 connector 组装出一条最小的原子 KMS
流水线，再让内核的 fbdev 模拟把这条流水线暴露成 `/dev/fb0`。本节以"导览"的
形式展开：

- 第 0、1 节建立心智模型（DRM/KMS 对象是什么、如何连接）——刚接触 DRM 的话
  从这里开始；第 1.1–1.5 节再逐个深入每个 KMS 对象；
- 第 2、3 节讲驱动如何注册、`/dev/fb0` 如何出现；
- 第 4 节先用大白话、再用精确的内核调用顺序，讲用户往 framebuffer 写像素时
  发生了什么；
- 第 5–7 节看原子提交机制、驱动的 `atomic_update()` 回调，并回答初学者常见
  问题；
- 后面的 "DRM ioctl 实战" 章节从用户态看同样的回调：`modetest` 一行命令和
  raw ioctl 示例程序。

所有函数名都能在编译本模块所用的内核源码（`~/microsoft/WSL2-Linux-Kernel`，
即 Makefile 里的 `KDIR`）中找到。相关文件列在第 8 节。

### 0. DRM 概念入门

**DRM 与 KMS。** DRM（Direct Rendering Manager）是内核中掌管显示的子系统，
分两半：*KMS*（Kernel Mode Setting）描述并编程显示流水线；*GEM*（Graphics
Execution Manager）管理存放像素的内存。本教程使用 KMS 对象模型加 GEM DMA
辅助函数。

**四个 KMS 对象。** 显示流水线是一条对象链：

| 对象 | 作用 | 日常类比 | 本驱动 |
| ---- | ---- | -------- | ------ |
| Framebuffer | 保存图像数据的内存 | 胶片 | GEM DMA 缓冲 |
| Plane | 选择一块 framebuffer 并放到屏幕上 | 投影仪幻灯片 | RGB565 主平面 |
| CRTC | 按固定时序扫描 plane 输出 | 投影仪的时钟/扫描头 | 固定 128x160 模式 |
| Encoder | 把扫描信号转换成 connector 需要的格式 | 信号转换盒 | `DRM_MODE_ENCODER_NONE`（空操作） |
| Connector | 物理接口；暴露显示支持的模式 | 插座 | 只有一个固定模式的虚拟 connector |

像素从 framebuffer 向上流经 plane → CRTC → encoder → connector；模式（分辨率）
则反方向流动：connector 的 `get_modes` 产生模式，CRTC 负责校验。

**fbdev 及其模拟。** fbdev 是 Linux 古老的 framebuffer API（`/dev/fb0`、
`ioctl(FBIOGET_VSCREENINFO)`、`mmap`……）。现代 DRM 驱动并不自己实现 fbdev，
而是由 DRM 提供一层 *fbdev 模拟*，在真实 DRM 流水线前面注册一个假的
`/dev/fb0`：老程序往里面写像素，模拟层在内部把这些写入翻译成正规的 DRM 操作。
本教程正是这么做的：`tests/fb_fill`、`tests/fb_pixel_set` 走的是老 API，而驱动
在底层执行的是现代的原子操作。

**原子模式设置。** 原子 KMS 不靠一堆 legacy ioctl，而是基于*状态（state）*：
构造一个 `drm_atomic_state` 描述期望的配置（哪个 framebuffer 上到哪个 plane、
哪个模式给哪个 CRTC……），先让 DRM *检查（check）*（检查失败就什么都不改），
再*提交（commit）*（一次性全部生效）。驱动通过 `atomic_check` / `atomic_update`
两个回调挂进这两个阶段——正是 `drm.c` 里实现的那两个回调。

**两个设备节点，同一个驱动。** 加载后你会看到 `/dev/dri/card0`（`modetest`、
合成器等使用的现代 DRM API）和 `/dev/fb0`（我们的测试程序和 fbcon 使用的
legacy API）。两者最终都走同一条驱动代码路径。

### 1. 本驱动的拓扑

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

- `mode_config` 是 DRM 设备的总机：保存所有对象的链表，以及每次提交都会经过的
  函数表（`fb_create`、`atomic_check`、`atomic_commit`）。
- **plane** 是帧的展示位置：引用一块 framebuffer，并携带 damage clips。
- **CRTC** 拥有 plane（`crtc->primary`）并定义时序；本驱动只接受固定 128x160
  模式。
- **encoder** 有 `possible_crtcs`，声明哪个 CRTC 可以驱动它。
- **connector** 提供模式列表并连到 encoder。
- **framebuffer** 是 KMS 对象与内存之间的胶水：保存几何信息（宽/高/pitch/
  格式）和 GEM 对象句柄（`fb->obj[0]`），真正的像素字节就在那个 GEM 对象里。

五个对象各有一节：1.1 framebuffer、1.2 plane、1.3 CRTC、1.4 encoder、1.5
connector。

#### 1.1 Framebuffer 与 GEM 内存

**作用。** framebuffer（`struct drm_framebuffer`）是对一块 2D 像素缓冲的描述：
宽和高、像素格式（RGB565）、pitch（每行字节数）、修饰符，以及一个或多个 GEM
对象引用（本驱动用 `fb->obj[0]`）。它就是第 0 节类比里的"胶片"：plane 要读的
像素数据。

**创建。** 所有 framebuffer 都经由 `mode_config.funcs->fb_create` 创建，本驱动
把它设为 `drm_gem_fb_create_with_dirty`：

- 用户态：`DRM_IOCTL_MODE_ADDFB2` → `drm_mode_addfb2()` → `fb_create`；
- fbdev 客户端：`drm_client_framebuffer_create()` → 同一个 `fb_create`（这就是
  `/dev/fb0` 的后端存储）。

`drm_gem_fb_create_with_dirty()` 会用 plane 的格式列表校验请求的格式，创建
`drm_framebuffer`，并挂上 `.dirty = drm_atomic_helper_dirtyfb`。正是这个钩子把
damage clips 变成写入路径里的原子提交（见第 4 节）。

**背后的内存。** `fb->obj[0]` 是一个 `struct drm_gem_object`；本驱动里它一定是
DMA GEM 对象：`to_drm_gem_dma_obj(obj)` 可以拿到 `dma_addr`（给硬件用）和
`vaddr`（内核 CPU 映射）。fbdev 客户端通过 `drm_gem_dma_dumb_create` 把它分配
成 *dumb buffer*：普通 CPU 可写内存，不涉及 GPU。

**生命周期。** 原子辅助函数在提交前后对 framebuffer 引用进行增删，所以只要有
plane 还指向它，framebuffer 就不会被释放。

内核文件：`drm_framebuffer.c`、`drm_gem_framebuffer_helper.c`、
`drm_gem_dma_helper.c`。

#### 1.2 Plane

**作用。** plane 选择一块 framebuffer 并放到屏幕上。真实驱动通常有多块 plane
（primary、cursor、overlay）；本驱动只有一块主平面：固定尺寸、RGB565、无缩放。

**在 drm.c 中。** `drm_tutorial_create_plane()`：

- `drm_universal_plane_init(dev, plane, 0, ...)` —— 这里 `possible_crtcs = 0`，
  稍后由 CRTC 补上（见 1.3）；
- 格式列表 `{ DRM_FORMAT_RGB565 }`，修饰符 `{ DRM_FORMAT_MOD_LINEAR,
  DRM_FORMAT_MOD_INVALID }` —— 只接受线性 RGB565 framebuffer；
- 类型 `DRM_PLANE_TYPE_PRIMARY`。

**回调。**

| 表 | 成员 | 值 | 何时调用 |
| -- | ---- | -- | -------- |
| `drm_plane_funcs` | `.reset` | `drm_gem_reset_shadow_plane` | 状态（重新）初始化 |
| | `.atomic_duplicate_state` | `drm_gem_duplicate_shadow_plane_state` | 提交前复制状态 |
| | `.atomic_destroy_state` | `drm_gem_destroy_shadow_plane_state` | 释放状态 |
| | `.update_plane` | `drm_atomic_helper_update_plane` | plane 更新 ioctl |
| | `.disable_plane` | `drm_atomic_helper_disable_plane` | 禁用 plane |
| `drm_plane_helper_funcs` | `.begin_fb_access` | `drm_gem_begin_shadow_fb_access` | `atomic_update` 之前：vmap framebuffer |
| | `.end_fb_access` | `drm_gem_end_shadow_fb_access` | `atomic_update` 之后：vunmap framebuffer |
| | `.atomic_check` | `drm_tutorial_plane_helper_atomic_check` | 原子检查阶段 |
| | `.atomic_update` | `drm_tutorial_plane_helper_atomic_update` | 原子提交阶段 |

由于状态回调都是 *shadow* 变体，plane 状态是 `struct drm_shadow_plane_state`：
标准 plane 状态之外还有 `map`/`data` 槽位，在提交进行期间保存 framebuffer 的
内核映射。

**Damage。** `drm_plane_enable_fb_damage_clips()` 添加标准的 `FB_DAMAGE_CLIPS`
属性。`drm_atomic_helper_dirtyfb()` 把 damage blob 写进
`plane_state->fb_damage_clips`；检查阶段把它转换成 `plane_state->damage`（见
第 5 节）。

**相邻关系。** plane ↔ CRTC：`crtc->primary` 和 `plane->possible_crtcs =
drm_crtc_mask(crtc)`（由 `drm_crtc_init_with_planes()` 设置）；plane ↔
framebuffer：`plane->state->fb`。

内核文件：`drm_plane.c`、`drm_gem_atomic_helper.c`。

#### 1.3 CRTC

**作用。** CRTC 掌管时序：按固定 128x160 模式逐行扫描主平面，为 encoder 产生
像素流。本驱动没有真实硬件，所以回调主要是校验和打日志。

**在 drm.c 中。** `drm_tutorial_create_crtc()`：

- `drm_crtc_init_with_planes(dev, crtc, &plane, NULL,
  &drm_tutorial_crtc_funcs, NULL)` 把 1.2 的 plane 绑成 `crtc->primary`；因为
  plane 创建时 `possible_crtcs = 0`，辅助函数会用 `drm_crtc_mask(crtc)` 补上
  （见 `drm_crtc.c`）。

**回调。**

| 表 | 成员 | 值 | 何时调用 |
| -- | ---- | -- | -------- |
| `drm_crtc_funcs` | `.reset` | `drm_atomic_helper_crtc_reset` | 状态（重新）初始化 |
| | `.set_config` | `drm_atomic_helper_set_config` | legacy `SETCONFIG` ioctl |
| | `.page_flip` | `drm_atomic_helper_page_flip` | `PAGE_FLIP` ioctl |
| | `.atomic_duplicate_state` / `.atomic_destroy_state` | 原子辅助函数 | 状态复制/释放 |
| `drm_crtc_helper_funcs` | `.mode_valid` | `drm_tutorial_crtc_helper_mode_valid` | `fill_modes` 期间的模式校验 |
| | `.atomic_check` | `drm_tutorial_crtc_helper_atomic_check` | 原子检查阶段 |
| | `.atomic_enable` / `.atomic_disable` | 只打日志 | 提交收尾阶段 |

**检查逻辑。** `drm_tutorial_crtc_helper_atomic_check()` 在 CRTC 启用时校验主
平面必须存在（`drm_atomic_helper_check_crtc_primary_plane()`），然后调用
`drm_atomic_add_affected_planes()` —— 任何涉及该 CRTC 的提交都会把它的 plane
一并拉进同一个原子状态。

**相邻关系。** CRTC → plane（`crtc->primary`）、CRTC ← encoder
（`encoder->possible_crtcs`）、CRTC ← 模式（`drm_crtc_helper_mode_valid_fixed`）。

内核文件：`drm_crtc.c`、`drm_atomic_helper.c`。

#### 1.4 Encoder

**作用。** encoder 把 CRTC 的像素流转换成 connector 需要的信号格式（LVDS、
HDMI TMDS……）。本教程没有真实信号，所以注册的是 `DRM_MODE_ENCODER_NONE` ——
一个透传占位符，仍然承担 CRTC 与 connector 之间的拓扑连接。

**在 drm.c 中。** `drm_tutorial_create_encoder()`：

- `drm_encoder_init(dev, encoder, &drm_tutorial_encoder_funcs,
  DRM_MODE_ENCODER_NONE, NULL)`；
- `encoder->possible_crtcs = drm_crtc_mask(crtc)` —— 唯一的那块 CRTC 可以驱动
  这个 encoder。

**回调。** 只有 `.destroy = drm_encoder_cleanup`；没有 `mode_valid`，也没有原子
钩子。encoder 的存在主要是为了拓扑和校验（`drm_encoder_mode_valid()` 因为钩子
为 NULL 而被直接跳过）。

**相邻关系。** encoder ↔ CRTC（`possible_crtcs`）、encoder ↔ connector
（`drm_connector_attach_encoder()` 双向建立链接）。

内核文件：`drm_encoder.c`、`drm_probe_helper.c`（`drm_encoder_mode_valid`）。

#### 1.5 Connector

**作用。** connector 代表物理接口，回答"这块显示能出哪些模式？"。本教程没有
真实显示，所以 connector 是虚拟的（`DRM_MODE_CONNECTOR_Unknown`），永远只上报
固定的 128x160 模式。

**在 drm.c 中。** `drm_tutorial_create_connector()`：

- `drm_connector_init(dev, connector, &drm_tutorial_connector_funcs,
  DRM_MODE_CONNECTOR_Unknown)`；
- `drm_connector_helper_add()` 挂上 `.get_modes =
  drm_tutorial_connector_get_modes`，它转发给 `drm_connector_helper_get_modes_fixed()`：
  复制固定模式、标记 `DRM_MODE_TYPE_PREFERRED`、加入 probed 模式列表；
- `drm_connector_attach_encoder()` —— 连接到 1.4 的 encoder。

**回调。**

| 表 | 成员 | 值 | 何时调用 |
| -- | ---- | -- | -------- |
| `drm_connector_funcs` | `.fill_modes` | `drm_helper_probe_single_connector_modes` | 模式探测（`GETCONNECTOR`、fbdev 客户端） |
| | `.reset` / `.atomic_duplicate_state` / `.atomic_destroy_state` | 原子辅助函数 | connector 状态 |
| `drm_connector_helper_funcs` | `.get_modes` | `drm_tutorial_connector_get_modes` | 填充 probed 模式列表 |

**模式流转。** `fill_modes` → `get_modes` → `__drm_helper_update_and_validate`
（依次校验 driver/size/flag/pipeline，链路末尾是 CRTC 的 `mode_valid`）→ 存活
下来的模式进入 `connector->modes`，供用户态和 fbdev 客户端选取。

**相邻关系。** connector ↔ encoder（`attach_encoder`）、connector ↔ 模式
（探测后的 `connector->modes`）、connector ↔ CRTC 通过 encoder 的
`possible_crtcs` 间接相连。

内核文件：`drm_connector.c`、`drm_probe_helper.c`。

#### 2. 模块加载与设备注册

```text
insmod drm-tutorial.ko
  └─ module_init(drm_tutorial_init)
       ├─ platform_device_register_simple("drm_tutorial")
       │     → 创建设备，驱动随后绑定到它
       └─ platform_driver_register(&drm_tutorial_platform_driver)
             → 驱动核心把它与已有设备匹配
             └─ drm_tutorial_probe()
```

`drm_tutorial_probe()` 按顺序执行：

1. `drm_dev_alloc(&drm_tutorial_driver, &pdev->dev)` —— 分配并初始化一个
   `struct drm_device`。静态的 `drm_tutorial_driver` 提供了核心需要的一切：
   - `.fops = DEFINE_DRM_GEM_DMA_FOPS(drm_tutorial_fops)` —— `/dev/dri/card*`
     的文件操作（open/release、mmap、ioctl 分发、DMA-BUF）；
   - `.dumb_create = drm_gem_dma_dumb_create` 和
     `.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table_vmap` ——
     GEM DMA 缓冲辅助函数；
   - `.fbdev_probe = drm_fbdev_dma_driver_fbdev_probe` —— 之后
     `drm_client_setup()` 要用的 fbdev 模拟入口；
   - `DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC` 特性位。
2. 填好全局 `s_drm_disp_mode` 固定模式：128x160（`clock = 1`、同步时序等于可见
   尺寸、物理尺寸 28x35 mm）。本教程驱动没有 EDID 或硬件探测，模式是硬编码的。
3. `drm_mode_config_init()` —— 初始化 `mode_config` 的链表、锁和对象 ID 分配器。
4. 设置模式配置边界：`min/max_width = 128`、`min/max_height = 160`、
   `preferred_depth = 16`。这个 depth 之后会决定 fbdev 的像素格式：16 bpp 就是
   RGB565。
5. `drm->mode_config.funcs = drm_tutorial_mode_config_funcs` —— 每次原子提交都
   会经过的全局操作表：
   - `.fb_create = drm_gem_fb_create_with_dirty` —— 该设备上创建的每个
     framebuffer 都会挂上 `.dirty = drm_atomic_helper_dirtyfb`（这是 fbdev 写入
     路径的关键钩子，见第 4 节）；
   - `.atomic_check = drm_atomic_helper_check` —— 通用原子校验；
   - `.atomic_commit = drm_atomic_helper_commit` —— 通用原子提交。
6. `drm->mode_config.helper_private = drm_tutorial_mode_config_helper_funcs`，
   其中 `.atomic_commit_tail = drm_atomic_helper_commit_tail` —— 提交被接受后
   真正"编程硬件"的收尾阶段。
7. 按依赖顺序创建四个 KMS 对象（对象如何互连见下一小节）：
   - `drm_tutorial_create_plane()`：
     - `drm_universal_plane_init()` 把 plane 注册为 `DRM_PLANE_TYPE_PRIMARY`，
       格式列表 RGB565、修饰符 LINEAR + INVALID；
     - plane 回调用的是 *shadow plane* 辅助函数：`.reset =
       drm_gem_reset_shadow_plane`、`.atomic_duplicate_state =
       drm_gem_duplicate_shadow_plane_state`、`.atomic_destroy_state =
       drm_gem_destroy_shadow_plane_state` —— plane 状态因此是
       `struct drm_shadow_plane_state`，可以在提交进行期间保存 framebuffer 的
       内核映射；
     - `.update_plane` / `.disable_plane` 是 DRM ioctl 使用的标准原子辅助函数
       入口；
     - `drm_plane_helper_add()` 安装 `.begin_fb_access =
       drm_gem_begin_shadow_fb_access`、`.end_fb_access =
       drm_gem_end_shadow_fb_access`，以及驱动自己的 `.atomic_check` /
       `.atomic_update`；
     - `drm_plane_enable_fb_damage_clips()` 添加标准 `FB_DAMAGE_CLIPS` plane
       属性。
   - `drm_tutorial_create_crtc()`：
     - `drm_crtc_init_with_planes(dev, crtc, &plane, NULL, ...)` 注册 CRTC 并写
       `crtc->primary = plane`（见 `drm_crtc.c`），上一步创建的 plane 成为该
       CRTC 的主平面；
     - CRTC 回调基于原子辅助函数（`.set_config =
       drm_atomic_helper_set_config`、`.page_flip = drm_atomic_helper_page_flip`、
       原子 reset/duplicate/destroy）；
     - `drm_crtc_helper_add()` 安装 `.mode_valid =
       drm_tutorial_crtc_helper_mode_valid`、驱动的 `.atomic_check`，以及
       `.atomic_enable` / `.atomic_disable`（目前只打日志）。
   - `drm_tutorial_create_encoder()`：
     - `drm_encoder_init(..., DRM_MODE_ENCODER_NONE, NULL)` 注册一个没有真实
       信号编码的 encoder；
     - `encoder->possible_crtcs = drm_crtc_mask(crtc)` —— 能被这个 encoder 驱动
       的 CRTC 位掩码；核心在流水线模式校验（`drm_mode_validate_pipeline`）时
       读取它。
   - `drm_tutorial_create_connector()`：
     - `drm_connector_init(..., DRM_MODE_CONNECTOR_Unknown)` 注册 connector；
     - `drm_connector_helper_add()` 安装 `.get_modes =
       drm_tutorial_connector_get_modes`，它转发给
       `drm_connector_helper_get_modes_fixed()`：复制固定 128x160 模式、标记
       `DRM_MODE_TYPE_PREFERRED`、加入 probed 模式列表；
     - `connector->funcs->fill_modes = drm_helper_probe_single_connector_modes`
       是用户态探测（`DRM_IOCTL_MODE_GETCONNECTOR`）和 fbdev 客户端填充
       `connector->modes` 共同调用的入口。
   - `drm_connector_attach_encoder(connector, encoder)` —— 把 connector 连到
     encoder（反之亦然，通过 encoder 的 connector 链表）。
8. `drm_mode_config_reset()` —— 为每个对象分配初始状态（通过各自的 `.reset`
   回调创建 CRTC/plane/connector 状态）。
9. `drm_dev_register()` —— 发布设备；创建字符设备节点（`/dev/dri/card0`）和
   sysfs 设备。
10. `drm_client_setup(drm, NULL)` —— 建立内核内客户端，也就是创建 `/dev/fb0`
    的 fbdev 模拟。详见第 3 节。

##### 四个对象是怎么粘在一起的

```text
                    drm_device
                        │
        ┌───────────────┴───────────────┐
   drm_connector                    drm_crtc
        │  attach_encoder               │  crtc->primary = plane
        ▼                               ▼
   drm_encoder ── possible_crtcs ── drm_crtc ── drm_plane
```

- **CRTC → plane**：`drm_crtc_init_with_planes()` 设置 `crtc->primary`，原子提交
  据此知道哪块 plane 负责显示扫描输出缓冲。
- **Encoder → CRTC**：`encoder->possible_crtcs = drm_crtc_mask(crtc)`。
  `drm_mode_validate_pipeline()` 用这个掩码决定选中的 encoder 能否驱动选中的
  CRTC。
- **Connector → encoder**：`drm_connector_attach_encoder()` 在两个对象里都记录
  链接。
- **Connector → 模式**：`fill_modes`（探测辅助函数）调用 connector 的
  `.get_modes` 构建模式列表；每个模式再沿流水线校验：
  `drm_mode_validate_driver()` → `drm_mode_validate_size()` →
  `drm_mode_validate_flag()` → `drm_mode_validate_pipeline()`，它会遍历
  connector → encoder → CRTC 并调用 `drm_crtc_mode_valid()`，最终到达驱动的
  `.mode_valid = drm_tutorial_crtc_helper_mode_valid`，转发给
  `drm_crtc_helper_mode_valid_fixed()`（128x160 返回 `MODE_OK`，否则返回
  `MODE_ONE_WIDTH` / `MODE_ONE_HEIGHT` / `MODE_ONE_SIZE`）。

时序图：模块加载 → probe → `/dev/dri/card0`（fbdev 部分在第 3 节继续）：

```text
module init          platform core       drm_tutorial_probe    DRM core
    │                     │                      │                 │
    │ platform_device_register_simple()          │                 │
    │────────────────────▶│                      │                 │
    │ platform_driver_register()                 │                 │
    │────────────────────▶│                      │                 │
    │                     │ probe("drm_tutorial")│                 │
    │                     │─────────────────────▶│                 │
    │                     │                      │ drm_dev_alloc() │
    │                     │                      │────────────────▶│
    │                     │                      │ mode_config_init│
    │                     │                      │────────────────▶│
    │                     │                      │ create plane,   │
    │                     │                      │ crtc, encoder,  │
    │                     │                      │ connector       │
    │                     │                      │────────────────▶│
    │                     │                      │ attach + reset  │
    │                     │                      │ drm_dev_register│
    │                     │                      │────────────────▶│
    │                     │                      │ drm_client_setup│ → /dev/fb0 (第 3 节)
    │                     │                      │────────────────▶│
```

#### 3. fbdev 模拟引导（`drm_client_setup()` → `/dev/fb0`）

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

最后这一调用就是 QEMU 章节上面那段 dmesg 的来源（
`drm_tutorial_crtc_helper_atomic_enable`，然后是重复出现的整屏 damage
`x1:0 y1:0 x2:128 y2:160` 的 `drm_tutorial_plane_helper_atomic_update`），接着是
`Console: switching to colour frame buffer device 16x20`。

注意：只有 framebuffer 是通过 `drm_gem_fb_create_with_dirty` 创建的（即
`fb->funcs->dirty` 已设置）时才会走 *shadowed* fbdev 路径。这正是驱动刻意在
mode config 里注册 `.fb_create = drm_gem_fb_create_with_dirty` 的原因。

时序图：从 `drm_client_setup()` 到 `/dev/fb0` 以及第一次 modeset：

```text
 fbdev client        fb helper           client modeset      connector           fbdev probe         fbcon / atomic
 (client_setup)      (initial_config)    (modeset_probe)     (.get_modes)        (dma probe)         (commit)
      │                    │                    │                   │                   │                   │
      │ drm_fbdev_client_setup()               │                   │                   │                   │
      │───────────────────▶│                    │                   │                   │                   │
      │                    │ hotplug → initial_config()           │                   │                   │
      │                    │───────────────────▶│                   │                   │                   │
      │                    │                    │ fill_modes()      │                   │                   │
      │                    │                    │──────────────────▶│                   │                   │
      │                    │                    │                   │ get_modes_fixed() │                   │
      │                    │                    │                   │ (128x160 added)   │                   │
      │                    │                    │ validate + pick_crtcs()              │                   │
      │                    │ single_fb_probe()  │                   │                   │                   │
      │                    │───────────────────────────────────────────────────────────▶│                   │
      │                    │                    │                   │                   │ fb_create → GEM   │
      │                    │                    │                   │                   │ shadow + defio    │
      │                    │ register_framebuffer()                │                   │                   │
      │                    │───────────────────────────────────────────────────────────▶│                   │
      │                    │                    │                   │                   │ /dev/fb0 created  │
      │                    │                    │                   │                   │ fbcon: set_par()  │
      │                    │                    │                   │                   │──────────────────▶│
      │                    │                    │                   │                   │                   │ atomic_commit → atomic_update()
```

#### 4. 用户向 `/dev/fb0` 写入，端到端

先用大白话讲一遍同样的旅程。把 `/dev/fb0` 想象成一本笔记本，它的真实页面由
DRM 掌控：

1. **写入落在草稿副本里。** 你的字节被复制进系统内存中的一块私有 *影子缓冲
   （shadow buffer）*。这一步很快、不需要 DRM 锁，所以 fbcon 随时可以绘图而不会
   阻塞显示流水线。真正的像素内存（GEM 缓冲）此刻还没被碰。
2. **内核记住脏矩形。** framebuffer 核心把你写入的字节范围换算成像素矩形，合并
   进辅助函数的 damage clip，然后调度一个后台 worker。写入是*批量*的：你的
   `write()` 立刻返回，真正的 DRM 工作在稍后发生。
3. **worker 只拷贝脏区域。** `drm_fbdev_dma_damage_blit` 把 clip 矩形从影子缓冲
   拷进 GEM 缓冲的内核映射。
4. **围绕 damage 构造一次提交。** worker 调用 framebuffer 的 `dirty` 钩子
   （`drm_atomic_helper_dirtyfb`），它会创建原子状态、把 clip 作为 plane 的
   `FB_DAMAGE_CLIPS` 属性挂上并提交。
5. **DRM 先检查，再提交。** 状态先经过 `atomic_check`（你的 plane/CRTC 检查
   回调），再进入提交机制，最终调用你 plane 的 `atomic_update()`——这是驱动编程
   硬件的时刻。本教程驱动只把看到的东西打日志。
6. **新旧状态的 damage 会被合并**，所以同一矩形即使被标记两次脏，也只会报告
   一次并覆盖两者。

每一步对应的精确内核函数见下面的图。`tests/fb_fill` 和 `tests/fb_pixel_set`
走的就是这条路径（它们用 `mmap()` 而不是 `write(2)`——那个变体在后面）：

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
                      └─ drm_atomic_commit(state)   → 第 5 节
```

测试程序用的 `mmap()` 路径走的是 deferred I/O：

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

fbcon 渲染走第三条等价路线：它的 `fb_fillrect` / `fb_copyarea` /
`fb_imageblit` 对应 `drm_fbdev_dma_shadowed_defio_*`，先画进影子缓冲，然后立刻
调用 `drm_fb_helper_damage_area()`。

要理解的关键点：**用户态从不直接写 GEM 缓冲**。它写进一块普通的
`vzalloc` 影子副本；一个 worker 累积脏矩形，flush 时把受影响区域拷进真正的 GEM
缓冲，并触发一次携带 damage clip（作为 plane 的 `FB_DAMAGE_CLIPS` 属性）的原子
提交。

时序图：一次 `write()` → `drm_tutorial_plane_helper_atomic_update()`：

```text
 userspace            fbdev core            damage worker         DRM core              driver hooks
 (tests)              (fbmem/fb_ops)        (workqueue)          (dirtyfb/atomic)      (drm.c)
      │                    │                      │                    │                    │
      │ write(2)           │                      │                    │                    │
      │───────────────────▶│                      │                    │                    │
      │                    │ fb_sys_write()       │                    │                    │
      │                    │ (→ shadow buffer)    │                    │                    │
      │                    │ damage_range()       │                    │                    │
      │                    │─────────────────────▶│                    │                    │
      │                    │                      │ merge clip;        │                    │
      │                    │                      │ schedule_work()    │                    │
      │                    │                      │                    │                    │
      │                    │                      │ damage_work()      │                    │
      │                    │                      │───────────────────▶│                    │
      │                    │                      │                    │ damage_blit:       │
      │                    │                      │                    │ shadow → GEM       │
      │                    │                      │ dirtyfb()          │                    │
      │                    │                      │───────────────────▶│                    │
      │                    │                      │                    │ atomic_commit()    │
      │                    │                      │                    │──────────────────▶│
      │                    │                      │                    │                    │ atomic_check()
      │                    │                      │                    │◀───────────────────│
      │                    │                      │                    │ atomic_update()    │
      │                    │                      │                    │──────────────────▶│
      │                    │                      │                    │                    │ (log damage rect)
```

测试程序用的 `mmap()` 路径不经过 `write(2)`，从 `fb_deferred_io_fault()` 进入；
从 `damage_range()` 起完全一样。

#### 5. 原子提交机制

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

两个值得注意的细节：

- `drm_atomic_helper_commit_planes()` 对状态中每个"新状态有 CRTC（或被禁用）"的
  plane 执行 `atomic_update`。因为 `drm_atomic_helper_swap_state()` 已经执行，
  在 `drm_tutorial_plane_helper_atomic_update()` 里 `plane->state` 是*新*状态；
  *旧*状态要用 `drm_atomic_get_old_plane_state(state, plane)` 取——这正是驱动在
  合并 damage 之前做的事。
- `begin_fb_access` / `end_fb_access` 这对 shadow 辅助函数包裹着更新：
  `drm_gem_begin_shadow_fb_access()` 把 framebuffer 对象 vmap 进内核地址空间，
  `drm_gem_end_shadow_fb_access()` 再 vunmap。

时序图：一次阻塞式原子提交的各阶段：

```text
 DRM core              atomic helpers         plane (driver)         crtc (driver)
 (drm_atomic.c)        (drm_atomic_helper.c)  (drm.c)                (drm.c)
      │                      │                      │                      │
      │ drm_atomic_check_only()                    │                      │
      │─────────────────────▶│                      │                      │
      │                      │ atomic_check()       │                      │
      │                      │─────────────────────▶│                      │
      │                      │ atomic_check()       │                      │
      │                      │────────────────────────────────────────────▶│
      │ drm_atomic_helper_commit()                 │                      │
      │─────────────────────▶│                      │                      │
      │                      │ prepare_planes()     │                      │
      │                      │  begin_fb_access()   │                      │
      │                      │─────────────────────▶│ (vmap)               │
      │                      │ swap_state()         │                      │
      │                      │ commit_tail()        │                      │
      │                      │  commit_planes()     │                      │
      │                      │   atomic_update()    │                      │
      │                      │─────────────────────▶│ (log damage)         │
      │                      │   end_fb_access()    │                      │
      │                      │─────────────────────▶│ (vunmap)             │
      │                      │  modeset_enables()   │                      │
      │                      │   atomic_enable()    │                      │
      │                      │────────────────────────────────────────────▶│ (log)
      │                      │  cleanup_planes()    │                      │
```

#### 6. `drm_tutorial_plane_helper_atomic_update()` 内部

这是驱动"编程硬件"的一步——真实设备上这里要写扫描输出寄存器。教程版改为检查
状态：

1. `if (!fb) return;` —— plane 可能被禁用，此时没有 framebuffer。
2. `drm_atomic_get_old_plane_state(state, plane)` —— 记住上一个状态；合并 damage
   时需要它。
3. `drm_dev_enter()` —— 防止设备被并发拔出（`drm_tutorial_remove()` 会调用
   `drm_dev_unplug()`）。
4. 从 framebuffer 走到后备内存：`fb->obj[0]` → `to_drm_gem_dma_obj(obj)` →
   `dma_obj->vaddr`。这是 GEM DMA 缓冲的内核虚拟地址——和第 3 节 fbdev 客户端
   vmap 的是同一个映射，所以通过 `/dev/fb0` 写入的像素在这里可见。
5. 打印 framebuffer 几何信息（`width`、`height`、`pitches[0]`、`vaddr`），并用
   `y * (fb->pitches[0] / 2) + x` 索引 dump 左上角 4x4 RGB565 像素（pitch 单位
   是字节，RGB565 占 2 字节）。
6. `drm_atomic_helper_damage_merged(old_plane_state, plane_state, &rect)` —— 把
   新旧 plane 状态的 damage 矩形合并成一个，然后打印 `x1,y1,x2,y2`。合并两个
   状态很重要：fbdev 客户端在 worker 运行前可能累积多个 dirtyfb clip；某个区域
   可能在新旧状态里都被标记脏，不合并就会重复或漏报。
7. `drm_dev_exit()` —— 与前面的 enter 配对。

#### 7. 初学者可能问的问题

**为什么我的写入不能立刻"看到"？** 本教程没有真实显示硬件，所以"看到"意味着
"出现在内核日志里"。即使在真实硬件上，fbdev 写入也是异步刷新的：damage worker
批量处理它们，deferred I/O 要等约 50 ms（`HZ / 20`）才 flush mmap 页面。

**为什么需要影子缓冲？** 三个原因：fbcon 和老程序可能随时戳 `/dev/fb0` 内存，
我们不想让每次写入都走 DRM；真正的 GEM 缓冲可能是 DMA 内存，任意上下文直接写
不安全；damage 跟踪需要一份稳定、CPU 可访问的副本做比对。

**为什么屏幕是 RGB565？** 驱动设置 `mode_config.preferred_depth = 16`；fbdev
客户端把它转成 `color_mode = 16`，`drm_driver_legacy_fb_format()` 把 16 bpp 映射
成 `DRM_FORMAT_RGB565`。plane 也只声明了 `DRM_FORMAT_RGB565`。

**`FB_DAMAGE_CLIPS` 是什么？** 由 `drm_plane_enable_fb_damage_clips()` 启用的
标准 plane 属性。`drm_atomic_helper_check_plane_damage()` 在检查阶段把它拷进
`plane_state->damage`，`drm_atomic_helper_damage_merged()` 在 `atomic_update()`
期间合并新旧状态的 damage。

**真实驱动的 `atomic_update()` 会做什么？** 读取 framebuffer 的 GEM DMA 地址
（`dma_obj->dma_addr`），编程扫描输出寄存器：framebuffer 地址、pitch、宽高、
像素格式，并处理 enable/disable。本教程只把同样的信息打日志。

**`modetest` 在哪一步参与？** `modetest` 打开 `/dev/dri/card0` 查询 connector
模式（`DRM_IOCTL_MODE_GETCONNECTOR`），走 `fill_modes` → `get_modes` → 模式
校验；然后设置模式，走的就是和 fbdev 写入完全相同的
`drm_atomic_helper_commit()` 路径。

#### 8. 内核源码对照

想在真实内核源码里跟着调用链走，看这些文件：

| 主题 | 文件 |
| ---- | ---- |
| fbdev 写入系统调用入口 | `drivers/video/fbdev/core/fbmem.c`、`fb_sys_fops.c` |
| deferred I/O（mmap 路径） | `drivers/video/fbdev/core/fb_defio.c`、`include/linux/fb.h` |
| fbdev 模拟（影子缓冲、blit） | `drivers/gpu/drm/drm_fbdev_dma.c` |
| fbdev 客户端（hotplug、初始配置） | `drivers/gpu/drm/clients/drm_fbdev_client.c`、`drm_client_setup.c` |
| fbdev 辅助（damage worker、probe） | `drivers/gpu/drm/drm_fb_helper.c` |
| 客户端 modeset（首次提交） | `drivers/gpu/drm/drm_client_modeset.c` |
| 模式探测/校验 | `drivers/gpu/drm/drm_probe_helper.c`、`drm_modes.c` |
| dirtyfb → 原子提交 | `drivers/gpu/drm/drm_damage_helper.c`、`drm_atomic.c` |
| 原子辅助（check/commit/planes） | `drivers/gpu/drm/drm_atomic_helper.c` |
| shadow plane 辅助 | `drivers/gpu/drm/drm_gem_atomic_helper.c`、`drm_gem_framebuffer_helper.c` |
| 对象注册（plane/CRTC/encoder/connector） | `drivers/gpu/drm/drm_plane.c`、`drm_crtc.c`、`drm_encoder.c`、`drm_connector.c` |
| ioctl 分发表 | `drivers/gpu/drm/drm_ioctl.c` |
| 原子 ioctl 处理、dumb 缓冲 | `drivers/gpu/drm/drm_atomic_uapi.c`、`drm_dumb_buffers.c` |

## DRM ioctl 实战

前面各节发生的一切都在 ioctl 背后。有两种方式实际调用它们：`modetest`
（来自 libdrm，不用写代码）和 `examples/drm/` 里的 raw ioctl 小程序。

### modetest：不写代码直接戳驱动

```bash
sudo apt install libdrm-tools   # 提供 modetest

modetest -M drm_tutorial -c   # 列出 connector 及其模式 (GETCONNECTOR)
modetest -M drm_tutorial -e   # 列出 encoder              (GETENCODER)
modetest -M drm_tutorial -p   # 列出 plane                (GETPLANERESOURCES / GETPLANE)
modetest -M drm_tutorial -s 32:128x160          # legacy modeset (SETCRTC)
modetest -M drm_tutorial -a -s 32@33:128x160    # 原子 modeset (MODE_ATOMIC)
```

connector 和 CRTC 的 ID（这里示例是 32 和 33）来自 `-c` 的输出；模式语法细节见
`modetest -h`。`make test` 已经用了 `-e` 变体。一次成功的 modeset 会在 dmesg
里表现为 `atomic_check` → `atomic_update` → `atomic_enable`——和 fbdev 写入走
同样的回调。

### ioctl → 驱动回调对照表

| ioctl | 内核处理函数 | 到达驱动的方式 |
| ----- | ------------ | -------------- |
| `MODE_GETRESOURCES` | `drm_mode_getresources` | 核心对象列表（无驱动代码） |
| `MODE_GETCONNECTOR` | `drm_mode_getconnector` | `connector->funcs->fill_modes` → `.get_modes`（`drm_connector_helper_get_modes_fixed`）→ CRTC `.mode_valid`（`drm_crtc_helper_mode_valid_fixed`） |
| `MODE_GETENCODER` | `drm_mode_getencoder` | 核心对象元数据 |
| `MODE_GETPLANERESOURCES` / `MODE_GETPLANE` | `drm_mode_getplane_res` / `drm_mode_getplane` | 核心对象元数据 |
| `MODE_CREATE_DUMB` | `drm_mode_create_dumb_ioctl` | `driver->dumb_create = drm_gem_dma_dumb_create` |
| `MODE_ADDFB2` | `drm_mode_addfb2_ioctl` | `mode_config.funcs->fb_create = drm_gem_fb_create_with_dirty` |
| `MODE_MAP_DUMB` | `drm_mode_mmap_dumb` | GEM DMA mmap |
| `MODE_SETCRTC` | `drm_mode_setcrtc` → `drm_mode_set_config_internal` | `crtc->funcs->set_config = drm_atomic_helper_set_config` → `drm_atomic_commit` → check/commit |
| `MODE_ATOMIC` | `drm_mode_atomic_ioctl` | `drm_atomic_commit` → 同一条 check/commit 路径 |

最后两行是重点：legacy `SETCRTC` ioctl 和现代原子 ioctl 都会汇入"工作原理"第 5
节描述的完全相同的机制。

### 示例程序

`examples/drm/` 里的程序只用原始 `ioctl()` 系统调用，不依赖 libdrm。它们需要
内核 UAPI 头文件（`sudo apt install libdrm-dev` 会提供
`/usr/include/drm/drm.h`）：

```bash
make -C examples/drm
sudo ./examples/drm/probe.out
sudo ./examples/drm/setcrtc.out
sudo ./examples/drm/atomic.out
```

**`probe.out`** 依次执行 `GETRESOURCES`，再对每个对象执行 `GETCONNECTOR` /
`GETENCODER` / `GETPLANE`，打印 ID 和模式。先跑它——打印出的 ID 就是其他工具
要用的数字。它也能直观展示 `GETCONNECTOR` 会走 `fill_modes`：教程驱动只会返回
一个 128x160 模式，标记为 `(preferred)`。

**`setcrtc.out`** 走 legacy 路径：创建 16 bpp dumb buffer（`CREATE_DUMB`）、作为
RGB565 framebuffer 挂上（`ADDFB2`）、通过 `MAP_DUMB` + `mmap` 填一个渐变，然后
带 connector 和模式调用 `SETCRTC`。dmesg 里应该看到 `atomic_check`、
`atomic_update` 和 `atomic_enable`——证明 legacy ioctl 被翻译成了原子提交。

**`atomic.out`** 演示现代 API：

1. 用 `MODE_OBJ_GETPROPERTIES` + `MODE_GETPROPERTY` 找出属性 ID（`FB_ID`、
   `CRTC_ID`、`MODE_ID`、`ACTIVE`）；
2. 用 `CREATEPROPBLOB` 创建包含 128x160 `drm_mode_modeinfo` 的模式 blob；
3. 为 plane、CRTC 和 connector 提交一个原子状态，先带
   `DRM_MODE_ATOMIC_TEST_ONLY`——dmesg 只有 `atomic_check`，什么都不编程；
4. 再提交真的 commit——dmesg 现在还会出现 `atomic_update`，因为缓冲填的是
   `0xf800`（红色），驱动的 4x4 像素 dump 会打印 `0xf800`。

注意：`SETCRTC` 和 `MODE_ATOMIC` 需要 DRM master，所以程序会调用
`DRM_IOCTL_SET_MASTER`（需要 root，且没有合成器占用设备）。

## 内核调试技巧

### WSL 内核崩溃日志

WSL 把崩溃转储放在 `C:\Users\Admin\AppData\Local\Temp\wsl-crashes`；打开最新的
`kernel-panic-xxxxxxxx.txt` 文件。

### 解析 panic RIP

给定一条 RIP，例如 `drm_atomic_connector_get_property+0x1a3/0x340`，你可以手工或
用自带脚本把它映射回源码行。

> 需要带符号的 `vmlinux`——至少编译一次内核就能得到。

#### 方式 A：手工

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

#### 方式 B：用自带脚本

```bash
# 把 RIP 解析成 file:line
./scripts/ga ~/microsoft/WSL2-Linux-Kernel/vmlinux drm_atomic_connector_get_property+0x1a3
# /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808

# 打印 file:line 前后各 8 行，高亮目标行
./scripts/pa /home/developer/microsoft/WSL2-Linux-Kernel/drivers/gpu/drm/drm_atomic_uapi.c:808
```
