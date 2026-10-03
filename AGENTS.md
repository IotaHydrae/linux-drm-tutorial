# Repository Guidelines

## Skills（本仓遵守）

本仓的一切工作遵循工作区 `../AGENTS.md` 约定的四份 skill。**摘要随仓携带**（离线可读），
完整版在工作区 `skills/`。

| skill | 本仓副本 | 一句话 |
| --- | --- | --- |
| Repository Exploration | [`skills/developer-repository-exprolation/Summary.md`](skills/developer-repository-exprolation/Summary.md) | 先理解再修改；证据优先于直觉 |
| Knowledge | [`skills/developer-knowledge/Summary.md`](skills/developer-knowledge/Summary.md) | 首屏结论、事实分级、信息预算、漂移检查 |
| Testing | [`skills/developer-testing/Summary.md`](skills/developer-testing/Summary.md) | tests/tools 分层、oracle 声明、退出码、N 次测量 |
| Code Quality | [`skills/developer-code-quality/Summary.md`](skills/developer-code-quality/Summary.md) | **能跑 ≠ 完成**；可读性有硬标准 |

### 动手前的四行闸门（**强制**）

改任何代码或配置**之前**先写出这四行 ✓。**第 1 行或第 4 行写不出来就停手** ✗ —— 那是在猜 ✗。

```text
已验证：<确认了什么，凭据是什么：代码/实测/构建日志>
仍未知：<还没确认的；不许用推测填空>
最小改动：<只改一处，为什么是这一处>
生效验证：<如何证明改动真的生效：探针 / grep 生成物 / 构建日志里的编译行>
```

**先确认仪器，再相信读数** ✓ —— 宏没被注入、文件没被编译、配置被 defconfig 覆盖，
这三件事的症状都是"结果莫名其妙" ✗。


> General knowledge-base, oracle, CLI and exit-code conventions live in the
> workspace [AGENTS.md](../AGENTS.md). This file only records what is specific
> to `linux-drm-tutorial`.

## Project structure

- `drm.c` + `Makefile` — the out-of-tree DRM/KMS kernel module
  (`obj-m += drm-tutorial.o`, `drm-tutorial-objs := drm.o`). `drm.c` is the only
  kernel source file.
- `tests/` — user-space framebuffer tests (`test_fb_*`) plus shared helpers in
  `tests/common/`, and the offline host tests (`test_*_{abi,uapi,build,cli}.py`).
  See [tests/README.md](tests/README.md) for the oracle of each test.
- `examples/drm/` — raw DRM-ioctl demos (`probe`, `setcrtc`, `atomic`) with no
  libdrm dependency.
- `scripts/` — kernel debugging helpers: `ga` (symbol+offset to source line),
  `pa` (print context around `file:line`).
- `tools/` — `fbview.py`, a live framebuffer viewer.
- `docs/` — the split knowledge base (design documents). `docs/README.md` is the
  index; every document has a `.zh-CN.md` mirror.
- `README.md` / `README.zh-CN.md` — build/run docs plus a documentation index;
  keep the two in sync.

## Architecture invariants

- Minimal atomic KMS platform driver: fixed **128x160** mode, **RGB565** primary
  plane with fb damage clips, GEM DMA buffers, fbdev emulation via
  `drm_client_setup()`. These values come from `drm.c` and are what the tests
  assert; if `drm.c` changes, update the tests and the docs together.
- Write path: `/dev/fb0` write → shadow buffer → damage worker →
  `drm_atomic_helper_dirtyfb` → `drm_atomic_commit` →
  `drm_tutorial_plane_helper_atomic_update()`. The callback tables in
  `docs/kms-objects.md` are transcribed from `drm.c`; treat `drm.c` as the
  authority.
- `drm.c` is kernel code. Keep it in kernel style (tabs, 8 columns) and do not
  modify it for test convenience; report suspected bugs instead of patching them
  as part of a test change.

## Build, test and development commands

```bash
make                    # build the module against KDIR
make KDIR=/path/to/build
make -C tests           # build the C framebuffer tests
make -C tests check-offline   # host-only tests, no module, no /dev/fb*
make -C examples/drm    # build the DRM ioctl demos (needs /usr/include/drm/drm.h)
make check              # offline checks only
```

- `KDIR` defaults to `/lib/modules/$(uname -r)/build`.
- `make test` rebuilds, `rmmod`/`insmod`s the module and dumps modes with
  `modetest -e`. It changes the running kernel — run it only deliberately.
- Preferred verification order here is **offline first**:
  `make check` must pass on a host with no DRM device and no loaded module.
- Hardware tests (`tests/test_fb_*.out`) need the module loaded and `/dev/fb0`;
  without them they exit `3` (`ENVIRONMENT_ERROR`), never `1` (`FAIL`).
- Optional manual run after `make` + `insmod`:
  `sudo ./tests/test_fb_fill.out /dev/fb0 f800`.

## Coding style & naming

- Kernel code (`drm.c`): kernel style — tabs (8 columns), `drm_tutorial_*`
  symbol prefix, kernel-style `.clang-format`/`.clangd` at the repo root.
- User-space C (`tests/`, `examples/drm/`): 2-space indentation, C99.
- Python (`tools/`, `tests/*.py`): PEP 8 with docstrings; GPL-2.0 SPDX header.
- Tests are named `test_<behaviour>`; reusable framebuffer access and result
  reporting live in `tests/common/`, not duplicated per test.

## Commit & pull request guidelines

- Use the prefix convention from git history: `drm:`, `scripts:`, `tools:`,
  `tests:`, `examples:`, `docs:`, `chore:`.
- One logical change per commit; messages in English with a concise summary line
  and a body explaining the why.
- Never commit build artifacts or local config (`.gitignore` covers `*.o`,
  `*.ko`, `*.out`, `compile_commands.json`).
- PRs: describe what changed and how it was verified (offline `make check`, plus
  dmesg evidence when hardware was involved); keep the change scoped to the
  component named in the prefix.

## 驱动工具的方式（与工作区规范同源）

- **不许盲目 `sleep`，不许 blanket 超时** ✓ —— 用**轮询就绪**（0.2 s 间隔）+ **秒级超时** ✓。
  硬件测试必须**显式定义就绪检测**，不要依赖"设备恰好已经跑着" ✓。
- 反例：`sleep 22` + `timeout 300` ⇒ 明明 0.4 s 就有结论的操作拖到几分钟 ✗。
- 正解：`usb.core.find` 轮询 ✓、控制请求 0.5 s 超时 ✓、shell 命令 `timeout 10` ✓。
