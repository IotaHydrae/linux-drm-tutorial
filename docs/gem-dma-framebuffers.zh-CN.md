[**English**](gem-dma-framebuffers.md) | [**简体中文**](gem-dma-framebuffers.zh-CN.md)

# Framebuffer 与 GEM DMA 内存

> 每个 framebuffer 都经由 `drm_gem_fb_create_with_dirty()` 创建，它会挂上
> `.dirty = drm_atomic_helper_dirtyfb`。正是这一个钩子，让 `/dev/fb0` 写入产生的
> damage clips 变成原子提交。

## TL;DR

- framebuffer（`struct drm_framebuffer`）描述一块 2D 缓冲：宽、高、格式
  （RGB565）、pitch、修饰符，以及一个或多个 GEM 对象引用（这里是
  `fb->obj[0]`）。
- 用户态（`MODE_ADDFB2`）和 fbdev 客户端（`drm_client_framebuffer_create()`）
  都经由同一个 `mode_config.funcs->fb_create` 创建它。
- 驱动设置 `.fb_create = drm_gem_fb_create_with_dirty`，因此每个 framebuffer 都
  带上 `fb->funcs->dirty = drm_atomic_helper_dirtyfb`。fbdev 模拟之所以走
  *shadowed* 写入路径，唯一原因就是它。
- `fb->obj[0]` 在这里一定是 DMA GEM 对象：`to_drm_gem_dma_obj(obj)` 暴露
  `dma_addr`（给硬件用）和 `vaddr`（内核 CPU 映射）。
- 原子辅助函数在提交前后对 framebuffer 引用进行增删，所以只要有 plane 还指向
  它，framebuffer 就不会被释放。

## 创建

所有 framebuffer 都经过 `mode_config.funcs->fb_create`，它被设为
`drm_gem_fb_create_with_dirty`：

- 用户态：`DRM_IOCTL_MODE_ADDFB2` → `drm_mode_addfb2()` → `fb_create`；
- fbdev 客户端：`drm_client_framebuffer_create()` → 同一个 `fb_create`（这就是
  `/dev/fb0` 的后端存储）。

`drm_gem_fb_create_with_dirty()` 会用 plane 的格式列表校验请求的格式，创建
`drm_framebuffer`，并挂上 `.dirty = drm_atomic_helper_dirtyfb`。正是这个钩子把
damage clips 变成写入路径里的原子提交；见
[fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) 和
[atomic-commit.zh-CN.md](atomic-commit.zh-CN.md)。

## 背后的内存

`fb->obj[0]` 是一个 `struct drm_gem_object`；本驱动里它一定是 DMA GEM 对象：

- `to_drm_gem_dma_obj(obj)` 可以拿到 `dma_addr`（给硬件用）和 `vaddr`（内核 CPU
  映射）；
- fbdev 客户端通过 `drm_gem_dma_dumb_create`（驱动的 `.dumb_create`）把它分配成
  *dumb buffer*：普通 CPU 可写内存，不涉及 GPU；
- 像素格式固定为 RGB565，因为 `mode_config.preferred_depth = 16`，且 plane 只
  声明了 `DRM_FORMAT_RGB565`。

## 生命周期

原子辅助函数在提交前后对 framebuffer 引用进行增删，所以只要有 plane 还指向它，
framebuffer 就不会被释放。提交期间 shadow 辅助函数
（`drm_gem_begin_shadow_fb_access` / `drm_gem_end_shadow_fb_access`）会把
`fb->obj[0]` 映射进内核地址空间再解除映射；见
[atomic-commit.zh-CN.md](atomic-commit.zh-CN.md)。

内核文件：`drm_framebuffer.c`、`drm_gem_framebuffer_helper.c`、
`drm_gem_dma_helper.c`。

## 相关

- [kms-objects.zh-CN.md](kms-objects.zh-CN.md) —— 消费 framebuffer 的 plane
- [fbdev-emulation.zh-CN.md](fbdev-emulation.zh-CN.md) —— fbdev 后端存储的分配
- [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) —— `dirty` 钩子被调用的地方
