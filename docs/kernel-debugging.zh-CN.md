[**English**](kernel-debugging.md) | [**简体中文**](kernel-debugging.zh-CN.md)

# 把内核 panic RIP 解析回源码行

> 给定一条 panic 记录，如 `drm_atomic_connector_get_property+0x1a3/0x340`，
> 用 `nm` + `addr2line` 可以把符号和偏移映射回 `file:line`。
> `scripts/ga` 与 `scripts/pa` 就是这两步的包装。

## TL;DR

- 你需要**带符号**的 `vmlinux`——至少编译一次内核。
- 手工：`nm vmlinux | grep <symbol>` → 加上偏移 → `addr2line -e vmlinux -i <addr>`。
- 或：`scripts/ga <vmlinux> <symbol+offset>` 打印 `addr2line` 的第一行（`-i`
  的内联展开被截断，因此得到的是最外层帧）。
- `scripts/pa <file>:<line> [context]` 打印目标行前后各 `context` 行，并用红色
  高亮目标行（默认 8）。
- 地址和行号是**特定构建**的；下面的例子是根 README 所述 WSL2 内核上的一次历史
  观察，换一个构建就对不上了。

## 手工方式

```bash
nm <kernel-src>/vmlinux | grep drm_atomic_connector_get_property
# ffffffff81d67c10 t drm_atomic_connector_get_property
# ffffffff81d67c00 t __pfx_drm_atomic_connector_get_property

# ffffffff81d67c10 + 0x1a3 = ffffffff81d67db3

addr2line -e <kernel-src>/vmlinux -i ffffffff81d67db3
# <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c:808

awk 'NR>=800 && NR<=816 {print NR, $0}' \
  <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c
```

符号查找会排除以 `__` 开头的符号，所以
`__pfx_drm_atomic_connector_get_property`（函数前置填充）不是被采用的那个。

## 用自带脚本

```bash
# 把 RIP 解析成 file:line
./scripts/ga <kernel-src>/vmlinux drm_atomic_connector_get_property+0x1a3
# <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c:808

# 打印 file:line 前后各 8 行，高亮目标行
./scripts/pa <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c:808
```

两个脚本都支持 `--help`，并使用工作区的退出码约定（见 `scripts/ga`、
`scripts/pa`）。

## 获取崩溃日志

WSL 把崩溃转储放在 Windows 侧的 `%LOCALAPPDATA%\Temp\wsl-crashes`；打开最新的
`kernel-panic-xxxxxxxx.txt` 文件。

## 边界与陷阱

- 上面的 `ffffffff81d67c10` / `+0x1a3` / `drm_atomic_uapi.c:808` 是在所述 WSL2
  内核上的一次历史观察；符号地址和行号会随每次构建与配置改变。
- 原教程在第 808 行展示的源码片段这里刻意省略了：它属于某一个内核修订版，再写
  一次只会再次漂移。

## 相关

- [kernel-source-map.zh-CN.md](kernel-source-map.zh-CN.md) —— 解析出的行所在文件
- [driver-faq.zh-CN.md](driver-faq.zh-CN.md) —— panic 栈里出现的回调
