[**English**](atomic-commit.md) | [**简体中文**](atomic-commit.zh-CN.md)

# 原子提交机制与驱动的 `atomic_update()`

> `drm_atomic_commit()` 先跑检查阶段（驱动的 `atomic_check` 回调），再跑提交
> 收尾：vmap framebuffer、调用驱动的 `atomic_update()`、再解除映射。
> `swap_state()` 在提交收尾之前执行，所以 `atomic_update()` 里的 `plane->state`
> 已经是新状态。

## TL;DR

- 检查阶段：`drm_atomic_helper_check()` → `drm_atomic_helper_check_planes()` →
  `drm_atomic_helper_check_plane_damage()`（blob → `plane_state->damage`），
  然后才是驱动的 plane 和 CRTC `atomic_check` 回调。
- 提交阶段：`drm_atomic_helper_setup_commit()` + `prepare_planes()` 执行
  `.begin_fb_access = drm_gem_begin_shadow_fb_access`（vmap），然后是
  `drm_atomic_helper_swap_state()`，最后是
  `drm_atomic_helper_commit_tail()`。
- `drm_atomic_helper_commit_planes()` 调用
  `drm_tutorial_plane_helper_atomic_update()`；之后 `end_fb_access` 把
  framebuffer vunmap。
- 因为 `swap_state()` 已经执行，`plane->state` 是*新*状态；*旧*状态要用
  `drm_atomic_get_old_plane_state(state, plane)` 取——这正是驱动在合并 damage
  之前做的事。
- 驱动的 `atomic_update()` 沿 `fb->obj[0]` → `to_drm_gem_dma_obj(obj)` →
  `dma_obj->vaddr` 走，打印几何信息和左上角 4x4 像素，然后合并并打印 damage
  矩形。

## 检查与提交调用树

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

`drm_atomic_helper_commit_planes()` 对状态中每个"新状态有 CRTC（或被禁用）"的
plane 执行 `atomic_update`。`begin_fb_access` / `end_fb_access` 这对辅助函数包裹
着更新：`drm_gem_begin_shadow_fb_access()` 把 framebuffer 对象 vmap 进内核地址
空间，`drm_gem_end_shadow_fb_access()` 再 vunmap。

## `drm_tutorial_plane_helper_atomic_update()` 内部

这是驱动"编程硬件"的一步——真实设备上这里要写扫描输出寄存器。教程版改为检查
状态：

1. `if (!fb) return;` —— plane 可能被禁用，此时没有 framebuffer。
2. `drm_atomic_get_old_plane_state(state, plane)` —— 记住上一个状态；合并 damage
   时需要它。
3. `drm_dev_enter()` —— 防止设备被并发拔出（`drm_tutorial_remove()` 会调用
   `drm_dev_unplug()`）。
4. 从 framebuffer 走到后备内存：`fb->obj[0]` → `to_drm_gem_dma_obj(obj)` →
   `dma_obj->vaddr`。这是 GEM DMA 缓冲的内核虚拟地址——和 fbdev 客户端 vmap 的
   是同一个映射，所以通过 `/dev/fb0` 写入的像素在这里可见。
5. 打印 framebuffer 几何信息（`width`、`height`、`pitches[0]`、`vaddr`），并用
   `y * (fb->pitches[0] / 2) + x` 索引 dump 左上角 4x4 RGB565 像素（pitch 单位
   是字节，RGB565 占 2 字节）。
6. `drm_atomic_helper_damage_merged(old_plane_state, plane_state, &rect)` —— 把
   新旧 plane 状态的 damage 矩形合并成一个，然后打印 `x1,y1,x2,y2`。合并两个
   状态很重要：fbdev 客户端在 worker 运行前可能累积多个 dirtyfb clip；某个区域
   可能在新旧状态里都被标记脏，不合并就会重复或漏报。
7. `drm_dev_exit()` —— 与前面的 enter 配对。

## 边界与陷阱

- 上面的辅助函数名是针对根 README 所述 WSL2 内核的，没有在其他内核版本上重新
  验证。

## 相关

- [fbdev-write-path.zh-CN.md](fbdev-write-path.zh-CN.md) —— 谁在这里调用 `drm_atomic_commit()`
- [kms-objects.zh-CN.md](kms-objects.zh-CN.md) —— 上面引用的各回调
- [driver-faq.zh-CN.md](driver-faq.zh-CN.md) —— 状态为什么是这样
