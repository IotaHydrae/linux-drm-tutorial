# SPDX-License-Identifier: GPL-2.0
#
# Framebuffer tests. See README.md for the oracle and source of every test.
#
#   make                 build the C tests (test_fb_*.out)
#   make check-offline   run the host-only tests (no module, no /dev/fb*)
#   make check-hardware  run the C tests against FBDEV (module must be loaded)
#   make clean           remove build products

CC       ?= cc
CFLAGS   ?= -Wall -Wextra -g -std=c99
CPPFLAGS += -Icommon

COMMON_SRCS := common/fbdev.c common/test_common.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)
TEST_SRCS   := $(wildcard test_*.c)
TEST_BINS   := $(TEST_SRCS:.c=.out)
PY_TESTS    := test_fb_uapi_abi.py test_drm_uapi_abi.py \
               test_examples_build.py test_tool_cli.py

FBDEV ?= /dev/fb0

.PHONY: all clean check-offline check-hardware help

all: $(TEST_BINS)

%.out: %.c $(COMMON_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(COMMON_OBJS)

common/%.o: common/%.c common/%.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

check-offline:
	@rc=0; \
	for t in $(PY_TESTS); do \
		echo "== $$t =="; \
		python3 $$t || rc=$$?; \
	done; \
	exit $$rc

check-hardware: all
	@if [ ! -e "$(FBDEV)" ]; then \
		echo "no $(FBDEV): load the module first (see ../README.md)"; \
		exit 3; \
	fi
	./test_fb_fill.out $(FBDEV) f800
	./test_fb_pixel_set.out $(FBDEV) 64 80 f800
	./test_fb_rectangle.out $(FBDEV)
	./test_fb_benchmark.out $(FBDEV) --duration 500

clean:
	rm -f $(TEST_BINS) $(COMMON_OBJS)

help:
	@sed -n '2,10p' Makefile
