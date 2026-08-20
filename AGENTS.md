# Repository Guidelines

## Project Structure & Module Organization

- `drm.c` + `Makefile` — the out-of-tree DRM/KMS kernel module (`obj-m += drm-tutorial.o`, `drm-tutorial-objs := drm.o`).
- `tests/` — user-space framebuffer programs (`fb_fill`, `fb_pixel_set`, `fb_rectangle`); every `*.c` builds to a matching `.out`.
- `examples/drm/` — raw DRM-ioctl demos (`probe`, `setcrtc`, `atomic`) with no libdrm dependency.
- `scripts/` — kernel debugging helpers: `ga` (symbol+offset to source line), `pa` (print context around `file:line`).
- `tools/` — `fbview.py`, a live framebuffer viewer.
- `README.md` / `README.zh-CN.md` — English and Simplified Chinese docs; keep both in sync.

## Build, Test, and Development Commands

- `make` — build the kernel module against `KDIR` (default `/lib/modules/$(uname -r)/build`).
- `make test` — rebuild, `rmmod`/`insmod` the module, then dump modes with `modetest -e`.
- `make -C tests` — build the framebuffer test binaries.
- `make -C examples/drm` — build the DRM ioctl demos (requires `/usr/include/drm/drm.h`).
- Run tests manually: `sudo ./tests/fb_fill.out f800` fills `/dev/fb0`; `sudo ./examples/drm/probe.out /dev/dri/cardN` enumerates objects.
- Verify in dmesg: `drm_tutorial_plane_helper_atomic_update: fb=...` logs the merged damage rect and the top-left 4x4 pixels.

## Coding Style & Naming Conventions

- Kernel code (`drm.c`): kernel style — tabs (8 columns), `drm_tutorial_*` symbol prefix, kernel-style `.clang-format`/`.clangd` at the repo root.
- User-space C (`tests/`, `examples/drm/`): 2-space indentation, C99.
- Python (`tools/`): PEP 8 with docstrings; GPL-2.0 SPDX header.

## Testing Guidelines

- No test framework; tests are standalone C binaries run with `sudo` against `/dev/fb0` or `/dev/dri/card*`.
- Naming: one behavior per program (`tests/fb_*.c`, `examples/drm/*.c`).
- Verify behavior through dmesg callbacks (`atomic_check` to `atomic_update` to `atomic_enable`) and the driver's pixel dump.
- `make clean` in each directory removes build artifacts (`*.out`, `*.o`, `*.ko`).

## Commit & Pull Request Guidelines

- Use the prefix convention from git history: `drm:`, `scripts:`, `tools:`, `tests:`, `examples:`, `docs:`, `chore:`.
- One logical change per commit; messages in English with a concise summary line and a body explaining the why.
- Never commit build artifacts or local config (`.gitignore` covers `*.o`, `*.ko`, `*.out`, `compile_commands.json`).
- PRs: describe what changed, how it was verified (dmesg output), and keep the change scoped to the component named in the prefix.

## Architecture Notes

- Minimal atomic KMS driver: fixed 128x160 mode, RGB565 primary plane with fb damage clips, GEM DMA buffers, fbdev emulation via `drm_client_setup()`.
- User writes to `/dev/fb0` flow through the shadow buffer, damage worker, `drm_atomic_helper_dirtyfb`, `drm_atomic_commit`, and finally `drm_tutorial_plane_helper_atomic_update`.
- The kernel source at `~/microsoft/WSL2-Linux-Kernel` is the build target and reference; see the "Kernel source map" section in the README.
