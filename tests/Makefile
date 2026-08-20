CC      = gcc
CFLAGS  = -Wall -g
SOURCES := $(wildcard ./*.c)
TARGETS := $(notdir $(SOURCES:.c=.out))

.PHONY: all clean

all: $(TARGETS)

%.out: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
