[**English**](drm-kms-concepts.md) | [**简体中文**](drm-kms-concepts.zh-CN.md)

# DRM/KMS concepts behind this tutorial

> KMS describes and programs the display pipeline as objects (framebuffer →
> plane → CRTC → encoder → connector); GEM manages the pixel memory; fbdev is
> the legacy API that DRM emulates in front of that pipeline.

## TL;DR

- **DRM** owns the display; it splits into **KMS** (mode setting) and **GEM**
  (graphics memory).
- Pixels flow **framebuffer → plane → CRTC → encoder → connector**; modes flow
  the other way (`get_modes` produces them, the CRTC validates them).
- **Atomic KMS** replaces legacy ioctls with a `drm_atomic_state`: build it,
  *check* it (nothing is applied if the check fails), then *commit* it. Drivers
  hook in with `atomic_check` / `atomic_update`.
- **fbdev** (`/dev/fb0`, `FBIOGET_VSCREENINFO`, `mmap`) is emulated by DRM; the
  legacy writes are translated into the atomic path internally.
- The driver exposes **two nodes**: `/dev/dri/card0` (modern) and `/dev/fb0`
  (legacy). Both reach the same `drm.c` callbacks.

## The four KMS objects

A display pipeline is a chain of objects:

| Object | Role | Everyday analogy | This driver |
| ------ | ---- | ---------------- | ----------- |
| Framebuffer | memory that holds the image data | the film | a GEM DMA buffer |
| Plane | picks a framebuffer and places it on screen | a projector slide | RGB565 primary plane |
| CRTC | scans the plane out at a fixed timing | the projector's clock / scanning head | fixed 128x160 mode |
| Encoder | converts the scanout signal for the connector | the signal converter box | `DRM_MODE_ENCODER_NONE` (no-op) |
| Connector | the plug; exposes the modes the display supports | the socket | virtual connector with one fixed mode |

Pixels flow up from the framebuffer through plane → CRTC → encoder →
connector. Modes (resolutions) flow the other way: the connector's `get_modes`
produces them and the CRTC validates them.

## fbdev and its emulation

*fbdev* is the legacy Linux framebuffer API (`/dev/fb0`,
`ioctl(FBIOGET_VSCREENINFO)`, `mmap`, ...). Modern DRM drivers do not implement
fbdev themselves. DRM ships an *fbdev emulation* layer that registers a fake
`/dev/fb0` in front of the real DRM pipeline: legacy programs write pixels to
it, and the emulation translates those writes into proper DRM operations.

This tutorial does exactly that: the tests in `tests/` talk to the old API
while the driver performs the modern atomic dance underneath.

## Atomic mode setting

Instead of a pile of legacy ioctls, atomic KMS works with *state*:

1. build a `drm_atomic_state` describing the desired configuration (which
   framebuffer on which plane, which mode on which CRTC, ...);
2. ask DRM to *check* it - nothing is applied if the check fails;
3. *commit* it - everything is applied at once.

Drivers hook into the two phases with `atomic_check` / `atomic_update`, the two
callbacks implemented in `drm.c`.

## Two device nodes, one driver

After loading, both nodes appear:

- `/dev/dri/card0` - the modern DRM API used by `modetest`, compositors, ...;
- `/dev/fb0` - the legacy API used by the tests and by fbcon.

Both end up in the same driver code paths; see
[driver-topology.md](driver-topology.md) for the graph.

## Related

- [driver-topology.md](driver-topology.md) - the concrete objects of this driver
- [fbdev-emulation.md](fbdev-emulation.md) - how `/dev/fb0` is created
- [atomic-commit.md](atomic-commit.md) - what check and commit actually run
