[**English**](driver-topology.md) | [**简体中文**](driver-topology.zh-CN.md)

# 本驱动的拓扑

> 一个 `drm_device` 持有 `mode_config`；四个对象被连成一条链
> **plane → crtc → encoder → connector**，模式沿这条链反向流动接受校验。用户态
> 通过 `/dev/dri/card0` 或模拟出来的 `/dev/fb0` 访问它。

## TL;DR

- `mode_config` 是设备的总机：保存对象链表，以及每次提交都会经过的函数表
  （`fb_create`、`atomic_check`、`atomic_commit`）。
- **framebuffer** 是 KMS 对象与内存之间的胶水：几何信息加上 `fb->obj[0]`，像素
  字节就在那里。
- **CRTC → plane** 通过 `crtc->primary`；**encoder → CRTC** 通过
  `encoder->possible_crtcs`；**connector → encoder** 通过
  `drm_connector_attach_encoder()`。
- 模式由 connector 产生，并沿流水线校验：`drm_mode_validate_driver()` →
  `_size()` → `_flag()` → `_pipeline()`，最后到达驱动的 `.mode_valid`。

## 对象图

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

- `mode_config` 保存所有对象的链表，以及函数表（`fb_create`、`atomic_check`、
  `atomic_commit`）。
- **plane** 是帧的展示位置：引用一块 framebuffer，并携带 damage clips。
- **CRTC** 拥有 plane（`crtc->primary`）并定义时序；本驱动只接受固定 128x160
  模式。
- **encoder** 有 `possible_crtcs`，声明哪个 CRTC 可以驱动它。
- **connector** 提供模式列表并连到 encoder。
- **framebuffer** 保存几何信息（宽/高/pitch/格式）和 GEM 对象句柄
  （`fb->obj[0]`），真正的像素字节就在那个 GEM 对象里。

## 四个对象是怎么粘在一起的

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
  据此知道哪块 plane 负责显示扫描输出缓冲。因为 plane 在
  `drm_tutorial_create_plane()` 里是用 `possible_crtcs = 0`
  （`drm_universal_plane_init()`）注册的，CRTC 辅助函数会补上
  `plane->possible_crtcs = drm_crtc_mask(crtc)`。
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

## 相关

- [kms-objects.zh-CN.md](kms-objects.zh-CN.md) —— 每个对象的构造与回调
- [module-load-and-probe.zh-CN.md](module-load-and-probe.zh-CN.md) —— 构建此图的代码
- [gem-dma-framebuffers.zh-CN.md](gem-dma-framebuffers.zh-CN.md) —— `fb->obj[0]` 是什么
