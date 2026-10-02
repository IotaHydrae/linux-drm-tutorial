[**English**](drm-ioctls.md) | [**简体中文**](drm-ioctls.zh-CN.md)

# DRM ioctl 实战

> 驱动做的一切都在 ioctl 背后。用 `modetest` 可以不写代码直接戳它，用
> `examples/drm/` 里的 raw ioctl 程序则能看清哪些请求真正到达驱动。`SETCRTC` 和
> `MODE_ATOMIC` 最终都汇入同一条 `drm_atomic_commit()` 路径。

## TL;DR

- `modetest -M drm_tutorial -c/-e/-p` 枚举 connector、encoder 和 plane；`-s`/`-a`
  设置模式。
- 一次成功的 modeset 会在 dmesg 里表现为 `atomic_check` → `atomic_update` →
  `atomic_enable`——和 fbdev 写入同样的回调。
- `MODE_GETCONNECTOR` 走 `fill_modes` → `.get_modes` → CRTC `.mode_valid`。
- `examples/drm/{probe,setcrtc,atomic}.out` 只用原始 `ioctl()`，不依赖 libdrm，
  需要 `/usr/include/drm/drm.h`（软件包：`libdrm-dev`）。
- `SETCRTC` 和 `MODE_ATOMIC` 需要 DRM master，所以示例会调用
  `DRM_IOCTL_SET_MASTER`（需要 root，且没有合成器占用设备）。

## modetest：不写代码直接戳驱动

```bash
sudo apt install libdrm-tools   # provides modetest

modetest -M drm_tutorial -c   # connectors and their modes (GETCONNECTOR)
modetest -M drm_tutorial -e   # encoders                  (GETENCODER)
modetest -M drm_tutorial -p   # planes                    (GETPLANERESOURCES / GETPLANE)
modetest -M drm_tutorial -s 32:128x160          # legacy modeset (SETCRTC)
modetest -M drm_tutorial -a -s 32@33:128x160    # atomic modeset (MODE_ATOMIC)
```

connector 和 CRTC 的 ID（这里示例是 32 和 33）来自 `-c` 的输出；模式语法细节见
`modetest -h`。`make test` 已经用了 `-e` 变体。一次成功的 modeset 会在 dmesg
里表现为 `atomic_check` → `atomic_update` → `atomic_enable`——和 fbdev 写入走
同样的回调。

## ioctl → 驱动回调对照表

| ioctl | 内核处理函数 | 到达驱动的方式 |
| ----- | ------------ | -------------- |
| `DRM_IOCTL_MODE_GETRESOURCES` | `drm_mode_getresources` | 核心对象列表（无驱动代码） |
| `DRM_IOCTL_MODE_GETCONNECTOR` | `drm_mode_getconnector` | `connector->funcs->fill_modes` → `.get_modes`（`drm_connector_helper_get_modes_fixed`）→ CRTC `.mode_valid`（`drm_crtc_helper_mode_valid_fixed`） |
| `DRM_IOCTL_MODE_GETENCODER` | `drm_mode_getencoder` | 核心对象元数据 |
| `DRM_IOCTL_MODE_GETPLANERESOURCES` / `DRM_IOCTL_MODE_GETPLANE` | `drm_mode_getplane_res` / `drm_mode_getplane` | 核心对象元数据 |
| `DRM_IOCTL_MODE_CREATE_DUMB` | `drm_mode_create_dumb_ioctl` | `driver->dumb_create = drm_gem_dma_dumb_create` |
| `DRM_IOCTL_MODE_ADDFB2` | `drm_mode_addfb2_ioctl` | `mode_config.funcs->fb_create = drm_gem_fb_create_with_dirty` |
| `DRM_IOCTL_MODE_MAP_DUMB` | `drm_mode_mmap_dumb` | GEM DMA mmap |
| `DRM_IOCTL_MODE_SETCRTC` | `drm_mode_setcrtc` → `drm_mode_set_config_internal` | `crtc->funcs->set_config = drm_atomic_helper_set_config` → `drm_atomic_commit` → check/commit |
| `DRM_IOCTL_MODE_ATOMIC` | `drm_mode_atomic_ioctl` | `drm_atomic_commit` → 同一条 check/commit 路径 |

最后两行是重点：legacy `SETCRTC` ioctl 和现代原子 ioctl 都会汇入
[atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) 描述的完全相同的机制。

## 示例程序

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

## 相关

- [atomic-commit.zh-CN.md](atomic-commit.zh-CN.md) —— 共享的 check/commit 路径
- [driver-faq.zh-CN.md](driver-faq.zh-CN.md) —— `modetest` 逐步做了什么
- [module-load-and-probe.zh-CN.md](module-load-and-probe.zh-CN.md) —— 被到达的各回调
