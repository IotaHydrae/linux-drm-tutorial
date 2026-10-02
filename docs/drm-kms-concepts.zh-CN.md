[**English**](drm-kms-concepts.md) | [**简体中文**](drm-kms-concepts.zh-CN.md)

# 本教程涉及的 DRM/KMS 概念

> KMS 用对象（framebuffer → plane → CRTC → encoder → connector）描述并编程显示
> 流水线；GEM 管理像素内存；fbdev 是 DRM 在这条流水线前面模拟的旧式 API。

## TL;DR

- **DRM** 掌管显示，分成两半：**KMS**（模式设置）和 **GEM**（图形内存）。
- 像素流向是 **framebuffer → plane → CRTC → encoder → connector**；模式
  （分辨率）反方向流动（`get_modes` 产生模式，CRTC 负责校验）。
- **原子 KMS** 用 `drm_atomic_state` 取代一堆 legacy ioctl：先构造状态，再*检查*
  （检查失败就什么都不改），最后*提交*。驱动通过 `atomic_check` / `atomic_update`
  挂进这两个阶段。
- **fbdev**（`/dev/fb0`、`FBIOGET_VSCREENINFO`、`mmap`）是 DRM 模拟出来的；旧式写入
  在内部被翻译成原子路径。
- 驱动暴露**两个节点**：`/dev/dri/card0`（现代）和 `/dev/fb0`（legacy）。两者最终
  都到达 `drm.c` 里同一批回调。

## 四个 KMS 对象

显示流水线是一条对象链：

| 对象 | 作用 | 日常类比 | 本驱动 |
| ---- | ---- | -------- | ------ |
| Framebuffer | 保存图像数据的内存 | 胶片 | GEM DMA 缓冲 |
| Plane | 选择一块 framebuffer 并放到屏幕上 | 投影仪幻灯片 | RGB565 主平面 |
| CRTC | 按固定时序扫描 plane 输出 | 投影仪的时钟/扫描头 | 固定 128x160 模式 |
| Encoder | 把扫描信号转换成 connector 需要的格式 | 信号转换盒 | `DRM_MODE_ENCODER_NONE`（空操作） |
| Connector | 物理接口；暴露显示支持的模式 | 插座 | 只有一个固定模式的虚拟 connector |

像素从 framebuffer 向上流经 plane → CRTC → encoder → connector；模式（分辨率）
则反方向流动：connector 的 `get_modes` 产生模式，CRTC 负责校验。

## fbdev 及其模拟

*fbdev* 是 Linux 古老的 framebuffer API（`/dev/fb0`、
`ioctl(FBIOGET_VSCREENINFO)`、`mmap`……）。现代 DRM 驱动并不自己实现 fbdev，
而是由 DRM 提供一层 *fbdev 模拟*，在真实 DRM 流水线前面注册一个假的
`/dev/fb0`：老程序往里面写像素，模拟层在内部把这些写入翻译成正规的 DRM 操作。

本教程正是这么做的：`tests/` 里的测试走老 API，而驱动在底层执行的是现代的原子
操作。

## 原子模式设置

原子 KMS 不靠一堆 legacy ioctl，而是基于*状态（state）*：

1. 构造一个 `drm_atomic_state` 描述期望的配置（哪个 framebuffer 上到哪个 plane、
   哪个模式给哪个 CRTC……）；
2. 先让 DRM *检查（check）*——检查失败就什么都不改；
3. 再*提交（commit）*——一次性全部生效。

驱动通过 `atomic_check` / `atomic_update` 两个回调挂进这两个阶段，正是 `drm.c`
里实现的那两个回调。

## 两个设备节点，同一个驱动

加载后两个节点都会出现：

- `/dev/dri/card0`——`modetest`、合成器等使用的现代 DRM API；
- `/dev/fb0`——测试程序和 fbcon 使用的 legacy API。

两者最终都走同一条驱动代码路径；对象图见
[driver-topology.md](driver-topology.zh-CN.md)。

## 相关

- [driver-topology.md](driver-topology.zh-CN.md) —— 本驱动的具体对象
- [fbdev-emulation.md](fbdev-emulation.zh-CN.md) —— `/dev/fb0` 如何创建
- [atomic-commit.md](atomic-commit.zh-CN.md) —— 检查与提交实际执行了什么
