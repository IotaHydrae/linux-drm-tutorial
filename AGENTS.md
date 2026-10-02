# Repository Guidelines

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
