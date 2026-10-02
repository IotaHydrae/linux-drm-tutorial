# Tests

> Read the workspace [AGENTS.md](../AGENTS.md) first: it defines the oracle,
> CLI and exit-code conventions this directory follows. This file only records
> **what each test here measures and where its expectation comes from**.

## TL;DR

- `make check` runs everything that needs **no kernel module and no `/dev/fb*`**.
  It must pass on a host with no DRM device at all.
- The C tests under `tests/` need the module loaded and `/dev/fb0`; run them with
  `make -C tests check-hardware`. Without a device they exit `3`
  (`ENVIRONMENT_ERROR`), **never `1`**.
- Reusable device access and reporting live in [`common/`](common/), not in each
  test.

## Running

```bash
make check                      # offline only: the four host tests below
make -C tests check-offline     # the same, from inside tests/
make -C tests                   # build the C tests (needs UAPI headers)
make -C tests check-hardware    # run them against FBDEV (default /dev/fb0)
make -C tests check-hardware FBDEV=/dev/fb1
```

## Host tests (no module, no framebuffer)

| Test | Oracle | Source of the expectation |
| --- | --- | --- |
| `test_fb_uapi_abi.py` | SPEC | `/usr/include/linux/fb.h`: `FBIOGET_VSCREENINFO == 0x4600`, `sizeof(struct fb_var_screeninfo) == 160`, and the `fb_bitfield`/`colourspace`/reserved offsets that `tools/fbview.py` hard-codes |
| `test_drm_uapi_abi.py` | SPEC | `/usr/include/drm/drm.h` + `drm_mode.h`: every `DRM_IOCTL_*` token used in `docs/` is still defined |
| `test_examples_build.py` | REQUIREMENT | [docs/drm-ioctls.md](../docs/drm-ioctls.md): `examples/drm/` uses only the raw `ioctl()` syscall, no libdrm — so it must build against the UAPI headers alone |
| `test_tool_cli.py` | REQUIREMENT | workspace AGENTS.md, "CLI and exit codes": `--help`/`--version` exit 0, invalid usage exits 2, a missing file/device exits 3 |

`test_fb_uapi_abi.py` and `test_drm_uapi_abi.py` are **drift guards**: they
compile a small probe against the *running* kernel's UAPI headers, so a renamed
or removed constant breaks the build rather than the viewer at runtime.

## Hardware tests (module loaded, `/dev/fb0` present)

These are **observations**, not specifications: they exercise a path and check a
relationship that must hold, which is what makes them useful without a golden
image.

| Test | Oracle | What it asserts |
| --- | --- | --- |
| `test_fb_fill.c` | RELATIONSHIP | after filling, the pixels read back are the colour that was written |
| `test_fb_pixel_set.c` | RELATIONSHIP + INVARIANT | a single written pixel reads back unchanged and its neighbours are untouched |
| `test_fb_rectangle.c` | RELATIONSHIP + INVARIANT | a drawn rectangle reads back inside its bounds and not outside them |
| `test_fb_benchmark.c` | NONE | writes for a fixed duration and reports the rate; **observation only** — no throughput threshold is defined anywhere, so it never declares PASS/FAIL |

`test_fb_benchmark.c` is the deliberate `ORACLE: NONE` case from the workspace
convention: measuring is useful, inventing a threshold for it is not.

## Shared helpers

- `common/fbdev.{c,h}` — open/map a framebuffer, read pixels back.
- `common/test_common.{c,h}` — result reporting for the C tests.
- `common/testlib.py` — the same conventions for the Python tests.

If two tests need the same device access or the same report shape, it belongs in
`common/` rather than in both tests.

## Exit codes

`0` PASS · `1` FAIL · `2` INVALID_USAGE · `3` ENVIRONMENT_ERROR · `4` TIMEOUT ·
`5` INCONCLUSIVE.

A missing module, a missing `/dev/fbN` or missing headers is an environment
problem. Reporting it as FAIL would make a broken bench look like a broken
driver.
