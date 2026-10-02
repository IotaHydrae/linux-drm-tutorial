obj-m += drm-tutorial.o

drm-tutorial-objs := drm.o

KDIR := /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

# Rebuild, reload and dump modes.  This changes the running kernel: run it
# deliberately, on a machine you can reboot.
test: all
	sudo rmmod drm-tutorial.ko || true
	sudo insmod drm-tutorial.ko || true
	modetest -e

# ---------------------------------------------------------------------------
# Offline checks: no module, no /dev/fb*, no DRM device.  This is the promise
# in AGENTS.md -- `make check` passes on a host with no hardware, and the cases
# that do need a device exit 3 (ENVIRONMENT_ERROR) rather than 1 (FAIL).
# See tests/README.md for the oracle behind each one.

.PHONY: all clean test check check-offline tests examples

check: check-offline

check-offline:
	$(MAKE) -C tests check-offline

# Build the C framebuffer tests (they need the module and /dev/fb0 to run).
tests:
	$(MAKE) -C tests

# Build the raw DRM-ioctl demos (kernel UAPI headers only, no libdrm).
examples:
	$(MAKE) -C examples/drm
