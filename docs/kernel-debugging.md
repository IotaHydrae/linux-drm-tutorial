[**English**](kernel-debugging.md) | [**简体中文**](kernel-debugging.zh-CN.md)

# Resolving a kernel panic RIP to a source line

> Given a panic line such as `drm_atomic_connector_get_property+0x1a3/0x340`,
> `nm` + `addr2line` map the symbol and offset back to a `file:line`.
> `scripts/ga` and `scripts/pa` wrap those two steps.

## TL;DR

- You need a `vmlinux` **with symbols** — build the kernel at least once.
- Manual: `nm vmlinux | grep <symbol>` → add the offset → `addr2line -e vmlinux -i <addr>`.
- Or: `scripts/ga <vmlinux> <symbol+offset>` prints the first `addr2line` line
  (`-i` inline expansions are cut, so you get the outermost frame).
- `scripts/pa <file>:<line> [context]` prints `context` lines before/after with
  the target line highlighted in red (default 8).
- Addresses and line numbers are **build-specific**; the example below is a
  historical observation from the documented WSL2 kernel and will not match a
  different build.

## Manual path

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

The symbol lookup excludes `__`-prefixed symbols, which is why
`__pfx_drm_atomic_connector_get_property` (the function-prefix padding) is not
the one that gets used.

## With the helper scripts

```bash
# resolve the RIP to a file:line
./scripts/ga <kernel-src>/vmlinux drm_atomic_connector_get_property+0x1a3
# <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c:808

# print 8 lines before and after file:line, highlighting the target line
./scripts/pa <kernel-src>/drivers/gpu/drm/drm_atomic_uapi.c:808
```

Both scripts accept `--help` and use the workspace exit codes (see
`scripts/ga`, `scripts/pa`).

## Get the crash log

WSL stores crash dumps under `%LOCALAPPDATA%\Temp\wsl-crashes` on the Windows
side; open the most recent `kernel-panic-xxxxxxxx.txt`.

## Caveats

- The `ffffffff81d67c10` / `+0x1a3` / `drm_atomic_uapi.c:808` values above are a
  single historical observation on the documented WSL2 kernel; symbol addresses
  and line numbers change with every build and config.
- The source excerpt that the original tutorial showed at line 808 is omitted
  here on purpose: it belongs to one kernel revision and would drift again.

## Related

- [kernel-source-map.md](kernel-source-map.md) - the files the resolved lines live in
- [driver-faq.md](driver-faq.md) - the callbacks that appear in panic traces
