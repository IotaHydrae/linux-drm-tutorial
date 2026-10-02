[**English**](kms-objects.md) | [**简体中文**](kms-objects.zh-CN.md)

# `drm.c` 的四个 KMS 对象

> `drm.c` 注册一块主平面、一块 CRTC、一个空操作 encoder 和一个虚拟 connector，
> 并为每个对象安装原子辅助回调。因为没有真实硬件，几乎所有回调都只是校验状态或
> 打日志。

## TL;DR

- Plane：只支持 RGB565、`DRM_PLANE_TYPE_PRIMARY`、*shadow* 状态辅助函数，并通过
  `drm_plane_enable_fb_damage_clips()` 启用 `FB_DAMAGE_CLIPS`。
- CRTC：把 plane 绑成 `crtc->primary`，用 `drm_crtc_helper_mode_valid_fixed()`
  校验固定模式，并把受影响的 plane 拉进同一个原子状态。
- Encoder：`DRM_MODE_ENCODER_NONE`；唯一实质内容是
  `possible_crtcs = drm_crtc_mask(crtc)`。
- Connector：`DRM_MODE_CONNECTOR_Unknown`；`get_modes` 转发给
  `drm_connector_helper_get_modes_fixed()`，永远只返回 128x160 的
  `DRM_MODE_TYPE_PREFERRED`。
- 下面的回调表就是内核实际能调用什么的准绳，直接取自 `drm.c`。

## Plane

**作用。** plane 选择一块 framebuffer 并放到屏幕上。真实驱动通常有多块 plane
（primary、cursor、overlay）；本驱动只有一块主平面：固定尺寸、RGB565、无缩放。

**在 `drm.c` 中。** `drm_tutorial_create_plane()`：

- `drm_universal_plane_init(dev, plane, 0, ...)` —— 这里 `possible_crtcs = 0`，
  稍后由 CRTC 补上（见 [driver-topology.zh-CN.md](driver-topology.zh-CN.md)）；
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
| | `.destroy` | `drm_plane_cleanup` | 对象销毁 |
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
[atomic-commit.zh-CN.md](atomic-commit.zh-CN.md)）。

**相邻关系。** plane ↔ CRTC：`crtc->primary` 和 `plane->possible_crtcs =
drm_crtc_mask(crtc)`（由 `drm_crtc_init_with_planes()` 设置）；plane ↔
framebuffer：`plane->state->fb`。

内核文件：`drm_plane.c`、`drm_gem_atomic_helper.c`。

## CRTC

**作用。** CRTC 掌管时序：按固定 128x160 模式逐行扫描主平面，为 encoder 产生
像素流。本驱动没有真实硬件，所以回调主要是校验和打日志。

**在 `drm.c` 中。** `drm_tutorial_create_crtc()`：

- `drm_crtc_init_with_planes(dev, crtc, &plane, NULL,
  &drm_tutorial_crtc_funcs, NULL)` 把 plane 绑成 `crtc->primary`；因为 plane
  创建时 `possible_crtcs = 0`，辅助函数会用 `drm_crtc_mask(crtc)` 补上
  （见 `drm_crtc.c`）。

**回调。**

| 表 | 成员 | 值 | 何时调用 |
| -- | ---- | -- | -------- |
| `drm_crtc_funcs` | `.reset` | `drm_atomic_helper_crtc_reset` | 状态（重新）初始化 |
| | `.set_config` | `drm_atomic_helper_set_config` | legacy `SETCONFIG` ioctl |
| | `.page_flip` | `drm_atomic_helper_page_flip` | `PAGE_FLIP` ioctl |
| | `.atomic_duplicate_state` / `.atomic_destroy_state` | 原子辅助函数 | 状态复制/释放 |
| | `.destroy` | `drm_crtc_cleanup` | 对象销毁 |
| `drm_crtc_helper_funcs` | `.mode_valid` | `drm_tutorial_crtc_helper_mode_valid` | `fill_modes` 期间的模式校验 |
| | `.atomic_check` | `drm_tutorial_crtc_helper_atomic_check` | 原子检查阶段 |
| | `.atomic_enable` / `.atomic_disable` | 只打日志 | 提交收尾阶段 |

**检查逻辑。** `drm_tutorial_crtc_helper_atomic_check()` 在 CRTC 被禁用时直接
返回；否则校验主平面必须存在（`drm_atomic_helper_check_crtc_primary_plane()`）。
两种路径最后都会调用 `drm_atomic_add_affected_planes()` —— 任何涉及该 CRTC 的
提交都会把它的 plane 一并拉进同一个原子状态。

**相邻关系。** CRTC → plane（`crtc->primary`）、CRTC ← encoder
（`encoder->possible_crtcs`）、CRTC ← 模式（`drm_crtc_helper_mode_valid_fixed`）。

内核文件：`drm_crtc.c`、`drm_atomic_helper.c`。

## Encoder

**作用。** encoder 把 CRTC 的像素流转换成 connector 需要的信号格式（LVDS、
HDMI TMDS……）。本教程没有真实信号，所以注册的是 `DRM_MODE_ENCODER_NONE` ——
一个透传占位符，仍然承担 CRTC 与 connector 之间的拓扑连接。

**在 `drm.c` 中。** `drm_tutorial_create_encoder()`：

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

## Connector

**作用。** connector 代表物理接口，回答"这块显示能出哪些模式？"。本教程没有
真实显示，所以 connector 是虚拟的（`DRM_MODE_CONNECTOR_Unknown`），永远只上报
固定的 128x160 模式。

**在 `drm.c` 中。** `drm_tutorial_create_connector()`：

- `drm_connector_init(dev, connector, &drm_tutorial_connector_funcs,
  DRM_MODE_CONNECTOR_Unknown)`；
- `drm_connector_helper_add()` 挂上 `.get_modes =
  drm_tutorial_connector_get_modes`，它转发给 `drm_connector_helper_get_modes_fixed()`：
  复制固定模式、标记 `DRM_MODE_TYPE_PREFERRED`、加入 probed 模式列表；
- `drm_connector_attach_encoder()` —— 连接到 encoder。

**回调。**

| 表 | 成员 | 值 | 何时调用 |
| -- | ---- | -- | -------- |
| `drm_connector_funcs` | `.fill_modes` | `drm_helper_probe_single_connector_modes` | 模式探测（`GETCONNECTOR`、fbdev 客户端） |
| | `.reset` / `.atomic_duplicate_state` / `.atomic_destroy_state` | 原子辅助函数 | connector 状态 |
| | `.destroy` | `drm_connector_cleanup` | 对象销毁 |
| `drm_connector_helper_funcs` | `.get_modes` | `drm_tutorial_connector_get_modes` | 填充 probed 模式列表 |

**模式流转。** `fill_modes` → `get_modes` → `__drm_helper_update_and_validate`
（依次校验 driver/size/flag/pipeline，链路末尾是 CRTC 的 `mode_valid`）→ 存活
下来的模式进入 `connector->modes`，供用户态和 fbdev 客户端选取。

**相邻关系。** connector ↔ encoder（`attach_encoder`）、connector ↔ 模式
（探测后的 `connector->modes`）、connector ↔ CRTC 通过 encoder 的
`possible_crtcs` 间接相连。

内核文件：`drm_connector.c`、`drm_probe_helper.c`。

## 相关

- [driver-topology.zh-CN.md](driver-topology.zh-CN.md) —— 这些对象如何互连
- [module-load-and-probe.zh-CN.md](module-load-and-probe.zh-CN.md) —— 创建它们的 probe 函数
- [atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) —— 各回调何时运行
