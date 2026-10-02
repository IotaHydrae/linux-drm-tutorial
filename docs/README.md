[**English**](README.md) | [**简体中文**](README.zh-CN.md)

# linux-drm-tutorial knowledge base

> Index of the split design documents. Start with the root [README.md](../README.md)
> for build and run instructions; come here for how the driver works.

## Scope

The KMS object model this driver builds, how a `/dev/fb0` write reaches
`drm_tutorial_plane_helper_atomic_update()`, and the observable dmesg call
paths. Statements about kernel-internal call chains are specific to the WSL2
kernel listed in the root README and are labelled where they cannot be
re-verified offline. Every document has a Simplified Chinese mirror
(`<name>.zh-CN.md`); the two must stay content-equal.

## Index

| Document | Content |
| -------- | ------- |
| [drm-kms-concepts.md](drm-kms-concepts.md) | DRM/KMS/GEM vocabulary, fbdev emulation, atomic modesetting, the two device nodes |
| [driver-topology.md](driver-topology.md) | This driver's object graph, `mode_config`, how plane/CRTC/encoder/connector are wired |
| [kms-objects.md](kms-objects.md) | plane, CRTC, encoder, connector: construction in `drm.c`, callback tables, neighbours |
| [gem-dma-framebuffers.md](gem-dma-framebuffers.md) | Framebuffer creation, GEM DMA memory, dumb buffers, the `dirty` hook |
| [module-load-and-probe.md](module-load-and-probe.md) | `insmod` → platform probe → `drm_tutorial_probe()` in 10 ordered steps |
| [fbdev-emulation.md](fbdev-emulation.md) | `drm_client_setup()` → `drm_fb_helper_*` → `/dev/fb0` and the first modeset |
| [fbdev-write-path.md](fbdev-write-path.md) | A user write to `/dev/fb0`: shadow buffer, damage worker, dirtyfb, mmap and fbcon variants |
| [atomic-commit.md](atomic-commit.md) | `drm_atomic_commit()` phases and what the driver's `atomic_update()` inspects |
| [driver-faq.md](driver-faq.md) | Short answers to the questions beginners ask about this driver |
| [drm-ioctls.md](drm-ioctls.md) | `modetest` recipes, the ioctl → driver callback map, the `examples/drm/` programs |
| [kernel-source-map.md](kernel-source-map.md) | Which kernel file implements which link of the call chains |
| [kernel-debugging.md](kernel-debugging.md) | Resolving a panic RIP to a source line with `addr2line` / `scripts/ga` / `scripts/pa` |

## Maintenance conventions

- **One document, one question.** Add a new document for a new question rather
  than growing an existing one past ~150 lines.
- **Code is authoritative.** If a document contradicts `drm.c`, `Makefile` or
  `examples/drm/*.c`, the code wins; fix the document in the same change.
- **Simplified Chinese mirrors must match.** Editing one language requires the
  same edit in the other.
- **No machine-local paths.** Use `<kernel-src>`, `<windows-share>` placeholders.
- **Label unverifiable claims.** Kernel-internal call chains tied to a specific
  kernel version say so; speculative statements say "not verified".
