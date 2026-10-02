[**English**](README.md) | [**简体中文**](README.zh-CN.md)

# linux-drm-tutorial 知识库

> 拆分后的设计文档索引。编译与运行说明看根目录
> [README.zh-CN.md](../README.zh-CN.md)；驱动原理看这里。

## 范围

本驱动构建的 KMS 对象模型、`/dev/fb0` 写入如何到达
`drm_tutorial_plane_helper_atomic_update()`，以及可观察的 dmesg 调用路径。涉及
内核内部调用链的陈述针对根 README 所列 WSL2 内核，无法离线复核的地方已标注。
每篇文档都有英文原件（`<name>.md`）；两者必须内容对等。

## 索引

| 文档 | 内容 |
| ---- | ---- |
| [drm-kms-concepts.zh-CN.md](drm-kms-concepts.zh-CN.md) | DRM/KMS/GEM 词汇、fbdev 模拟、原子模式设置、两个设备节点 |
| [driver-topology.zh-CN.md](driver-topology.zh-CN.md) | 本驱动的对象图、`mode_config`、plane/CRTC/encoder/connector 如何互连 |
| [kms-objects.zh-CN.md](kms-objects.zh-CN.md) | plane、CRTC、encoder、connector：`drm.c` 中的构造、回调表、相邻关系 |
| [gem-dma-framebuffers.zh-CN.md](gem-dma-framebuffers.zh-CN.md) | framebuffer 创建、GEM DMA 内存、dumb buffer、`dirty` 钩子 |
| [module-load-and-probe.zh-CN.md](module-load-and-probe.zh-CN.md) | `insmod` → 平台 probe → `drm_tutorial_probe()` 的 10 个有序步骤 |
| [fbdev-emulation.zh-CN.md](fbdev-emulation.zh-CN.md) | `drm_client_setup()` → `drm_fb_helper_*` → `/dev/fb0` 与第一次 modeset |
| [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) | 用户写入 `/dev/fb0`：影子缓冲、damage worker、dirtyfb、mmap 与 fbcon 变体 |
| [atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) | `drm_atomic_commit()` 的各阶段与驱动的 `atomic_update()` 检查了什么 |
| [driver-faq.zh-CN.md](driver-faq.zh-CN.md) | 初学者对本驱动常问问题的简短回答 |
| [drm-ioctls.zh-CN.md](drm-ioctls.zh-CN.md) | `modetest` 配方、ioctl → 驱动回调对照表、`examples/drm/` 程序 |
| [kernel-source-map.zh-CN.md](kernel-source-map.zh-CN.md) | 各条调用链由哪个内核文件实现 |
| [kernel-debugging.zh-CN.md](kernel-debugging.zh-CN.md) | 用 `addr2line` / `scripts/ga` / `scripts/pa` 解析 panic RIP |

## 维护约定

- **一文档一问题。** 有新问题就新增文档，不要把既有文档撑过约 150 行。
- **以代码为准。** 文档若与 `drm.c`、`Makefile` 或 `examples/drm/*.c` 冲突，以代码
  为准，并在同一次改动里修正文档。
- **中英镜像必须对齐。** 改一种语言就要同样地改另一种。
- **不写本机路径。** 用 `<kernel-src>`、`<windows-share>` 占位符。
- **标注不可复核的断言。** 绑定到特定内核版本的内核内部调用链要写明版本；推测性
  陈述要写"未验证"。
