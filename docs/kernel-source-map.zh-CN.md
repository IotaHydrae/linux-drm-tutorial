[**English**](kernel-source-map.md) | [**简体中文**](kernel-source-map.zh-CN.md)

# 内核源码对照

> 本知识库里各条调用链对应的代码在哪。路径相对于编译本模块所用的内核源码树；
> 目录名是针对根 README 所述内核版本的，没有在其他版本上重新验证。

## TL;DR

- fbdev 入口在 `drivers/video/fbdev/core/` 下。
- fbdev 模拟、fb helper 和客户端在 `drivers/gpu/drm/` 下。
- 原子辅助函数和 dirtyfb 路径在 `drm_atomic_helper.c`、`drm_damage_helper.c` 和
  `drm_atomic.c` 里。

## 对照表

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

## 相关

- [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) —— 这些文件实现的写入链
- [atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) —— 提交链
- [kernel-debugging.zh-CN.md](kernel-debugging.zh-CN.md) —— 把 panic 地址解析到这些文件
