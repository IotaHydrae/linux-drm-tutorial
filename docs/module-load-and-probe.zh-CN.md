[**English**](module-load-and-probe.md) | [**简体中文**](module-load-and-probe.zh-CN.md)

# 模块加载与设备注册

> `insmod` 注册一个平台设备和一个平台驱动；驱动核心把它们匹配起来并调用
> `drm_tutorial_probe()`，后者分配 `drm_device`、构建 KMS 对象、注册 DRM 设备，
> 最后建立 fbdev。

## TL;DR

- 本模块是一个普通的平台驱动：`platform_device_register_simple()` 加
  `platform_driver_register()`，按名字 `"drm_tutorial"` 匹配。
- `drm_tutorial_probe()` 按固定顺序执行：分配 → 模式配置 → 创建对象 → 连接 →
  reset → `drm_dev_register()` → `drm_client_setup()`。
- 模式是硬编码的（没有 EDID、没有探测）：128x160、`clock = 1`、物理尺寸
  28x35 mm。
- `preferred_depth = 16` 就是 fbdev 像素格式为 RGB565 的原因。
- `mode_config.funcs`（`fb_create`、`atomic_check`、`atomic_commit`）和
  `helper_private->atomic_commit_tail` 是每次提交都会经过的表。

## 从 `insmod` 到 `probe()`

```text
insmod drm-tutorial.ko
  └─ module_init(drm_tutorial_init)
       ├─ platform_device_register_simple("drm_tutorial")
       │     → creates the platform device the driver will bind to
       └─ platform_driver_register(&drm_tutorial_platform_driver)
             → driver core matches it against the existing device
             └─ drm_tutorial_probe()
```

## `drm_tutorial_probe()` 按顺序做的事

1. `drm_dev_alloc(&drm_tutorial_driver, &pdev->dev)` —— 分配并初始化一个
   `struct drm_device`。静态的 `drm_tutorial_driver` 提供了核心需要的一切：
   - `.fops = DEFINE_DRM_GEM_DMA_FOPS(drm_tutorial_fops)` —— `/dev/dri/card*`
     的文件操作（open/release、mmap、ioctl 分发、DMA-BUF）；
   - `.dumb_create = drm_gem_dma_dumb_create` 和
     `.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table_vmap` ——
     GEM DMA 缓冲辅助函数；
   - `.fbdev_probe = drm_fbdev_dma_driver_fbdev_probe` —— 之后
     `drm_client_setup()` 要用的 fbdev 模拟入口（当
     `CONFIG_DRM_FBDEV_EMULATION` 关闭时它会被编译成 `NULL`）；
   - `DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC` 特性位。
2. 填好全局 `s_drm_disp_mode` 固定模式：128x160（`clock = 1`、同步时序等于可见
   尺寸、物理尺寸 28x35 mm）。没有 EDID 或硬件探测，模式是硬编码的。
3. `drm_mode_config_init()` —— 初始化 `mode_config` 的链表、锁和对象 ID 分配器。
4. 设置模式配置边界：`min/max_width = 128`、`min/max_height = 160`、
   `preferred_depth = 16`。这个 depth 之后会决定 fbdev 的像素格式：16 bpp 就是
   RGB565。
5. `drm->mode_config.funcs = drm_tutorial_mode_config_funcs` —— 每次原子提交都
   会经过的全局操作表：
   - `.fb_create = drm_gem_fb_create_with_dirty` —— 该设备上创建的每个
     framebuffer 都会挂上 `.dirty = drm_atomic_helper_dirtyfb`（fbdev 写入路径的
     关键钩子）；
   - `.atomic_check = drm_atomic_helper_check` —— 通用原子校验；
   - `.atomic_commit = drm_atomic_helper_commit` —— 通用原子提交。
6. `drm->mode_config.helper_private = drm_tutorial_mode_config_helper_funcs`，
   其中 `.atomic_commit_tail = drm_atomic_helper_commit_tail` —— 提交被接受后
   真正"编程硬件"的收尾阶段。
7. 按依赖顺序创建四个 KMS 对象（详见
   [kms-objects.zh-CN.md](kms-objects.zh-CN.md)）：
   - `drm_tutorial_create_plane()` —— `drm_universal_plane_init()` 以
     `DRM_PLANE_TYPE_PRIMARY`、RGB565 和 LINEAR + INVALID 修饰符列表注册；
     然后 `drm_plane_helper_add()` 安装 shadow plane 回调，
     `drm_plane_enable_fb_damage_clips()` 启用 damage clips；
   - `drm_tutorial_create_crtc()` —— `drm_crtc_init_with_planes(dev, crtc,
     &plane, NULL, ...)` 写入 `crtc->primary = plane`；然后
     `drm_crtc_helper_add()` 安装 `.mode_valid`、`.atomic_check` 以及只打日志的
     `.atomic_enable` / `.atomic_disable`；
   - `drm_tutorial_create_encoder()` —— `drm_encoder_init(...,
     DRM_MODE_ENCODER_NONE, NULL)` 和
     `encoder->possible_crtcs = drm_crtc_mask(crtc)`；
   - `drm_tutorial_create_connector()` —— `drm_connector_init(...,
     DRM_MODE_CONNECTOR_Unknown)`、`drm_connector_helper_add()` 安装
     `.get_modes`，以及 `connector->funcs->fill_modes =
     drm_helper_probe_single_connector_modes`；
   - `drm_connector_attach_encoder(connector, encoder)` —— 把 connector 连到
     encoder（反之亦然，通过 encoder 的 connector 链表）。
8. `drm_mode_config_reset()` —— 为每个对象分配初始状态（通过各自的 `.reset`
   回调创建 CRTC/plane/connector 状态）。
9. `drm_dev_register()` —— 发布设备；创建字符设备节点（`/dev/dri/card0`）和
   sysfs 设备。
10. `drm_client_setup(drm, NULL)` —— 建立内核内客户端，也就是创建 `/dev/fb0`
    的 fbdev 模拟；见 [fbdev-emulation.zh-CN.md](fbdev-emulation.zh-CN.md)。

## 相关

- [driver-topology.zh-CN.md](driver-topology.zh-CN.md) —— 最终得到的对象图
- [kms-objects.zh-CN.md](kms-objects.zh-CN.md) —— 每个对象的回调表
- [fbdev-emulation.zh-CN.md](fbdev-emulation.zh-CN.md) —— 第 10 步的细节
