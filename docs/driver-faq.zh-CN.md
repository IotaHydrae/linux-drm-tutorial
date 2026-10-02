[**English**](driver-faq.md) | [**简体中文**](driver-faq.zh-CN.md)

# 教程驱动常见问题

> 本驱动最常引出的问题的简短回答。每个回答都以 `drm.c`、内核 DRM 辅助函数或
> [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) 描述的行为为依据。

## TL;DR

- 写入不会立刻可见：fbdev 写入是异步刷新的。
- 影子缓冲存在的意义：`/dev/fb0` 可以被随时戳写而不必每次写成 DRM，同时 damage
  跟踪有一份稳定、CPU 可访问的副本。
- RGB565 来自 `mode_config.preferred_depth = 16`。
- `FB_DAMAGE_CLIPS` 是标准 plane 属性；检查阶段把它拷进 `plane_state->damage`，
  `atomic_update()` 合并新旧 damage。
- `modetest` 到达的是和 fbdev 写入同一个 `drm_atomic_helper_commit()` 路径。

## 为什么我的写入不能立刻"看到"？

本教程没有真实显示硬件，所以"看到"意味着"出现在内核日志里"。即使在真实硬件上，
fbdev 写入也是异步刷新的：damage worker 批量处理它们，deferred I/O 会延迟
flush `mmap` 的页面。

## 为什么需要影子缓冲？

三个原因：

- fbcon 和老程序可能随时戳 `/dev/fb0` 内存，我们不想让每次写入都走 DRM；
- 真正的 GEM 缓冲可能是 DMA 内存，任意上下文直接写不安全；
- damage 跟踪需要一份稳定、CPU 可访问的副本做比对。

## 为什么屏幕是 RGB565？

驱动设置 `mode_config.preferred_depth = 16`；fbdev 客户端把它转成
`color_mode = 16`，`drm_driver_legacy_fb_format()` 把 16 bpp 映射成
`DRM_FORMAT_RGB565`。plane 也只声明了 `DRM_FORMAT_RGB565`。

## `FB_DAMAGE_CLIPS` 是什么？

由 `drm_plane_enable_fb_damage_clips()` 启用的标准 plane 属性。
`drm_atomic_helper_check_plane_damage()` 在检查阶段把它拷进
`plane_state->damage`，`drm_atomic_helper_damage_merged()` 在 `atomic_update()`
期间合并新旧状态的 damage。

## 真实驱动的 `atomic_update()` 会做什么？

读取 framebuffer 的 GEM DMA 地址（`dma_obj->dma_addr`），编程扫描输出寄存器：
framebuffer 地址、pitch、宽高、像素格式，并处理 enable/disable。本教程只把同样的
信息打日志。

## `modetest` 在哪一步参与？

`modetest` 打开 `/dev/dri/card0` 查询 connector 模式
（`DRM_IOCTL_MODE_GETCONNECTOR`），走 `fill_modes` → `get_modes` → 模式校验；
然后设置模式，走的就是和 fbdev 写入完全相同的 `drm_atomic_helper_commit()` 路径。

## 相关

- [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) —— 异步写入路径
- [atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) —— damage 合并的位置
- [drm-ioctls.zh-CN.md](drm-ioctls.zh-CN.md) —— 从用户态操作驱动
