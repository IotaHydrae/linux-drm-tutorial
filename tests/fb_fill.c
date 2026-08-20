/*
  fb_fill                    Linux Framebuffer RGB565 Filler
  Usage: fb_fill [fb_device] color
  Example: sudo ./tests/fb_fill.out /dev/fb0 07e0
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
  int fd;
  struct fb_var_screeninfo info;
  size_t len;
  unsigned char *buffer, *data;
  unsigned int color, i, j;
  char *end;
  char *fb_path = "/dev/fb0";

  if (argc == 2) {
    color = strtoul(argv[1], &end, 16);
    if (end == argv[1] || *end != '\0' || color > 0xffff) {
      printf("Invalid RGB565 color: %s\n", argv[1]);
      return 1;
    }
  } else if (argc == 3) {
    fb_path = argv[1];
    color = strtoul(argv[2], &end, 16);
    if (end == argv[2] || *end != '\0' || color > 0xffff) {
      printf("Invalid RGB565 color: %s\n", argv[2]);
      return 1;
    }
  } else {
    printf("Usage: %s [fb_device] color\n", argv[0]);
    return 1;
  }

  fd = open(fb_path, O_RDWR);
  if (fd == -1) {
    printf("Failed to open Framebuffer device: %m\n");
    return 1;
  }

  ioctl(fd, FBIOGET_VSCREENINFO, &info);

  if (info.bits_per_pixel != 16) {
    printf("Unsupported bits_per_pixel: %u (expected 16 for RGB565)\n",
           info.bits_per_pixel);
    close(fd);
    return 1;
  }

  len = info.xres * info.yres * info.bits_per_pixel / 8;
  buffer = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  data = buffer;
  for (i = 0; i < info.yres; i++) {
    for (j = 0; j < info.xres; j++) {
      *((unsigned short *)data) = (unsigned short)color;
      data += info.bits_per_pixel / 8;
    }
  }

  printf("Filled %ux%u with RGB565 0x%04x\n", info.xres, info.yres, color);

  munmap(buffer, len);
  close(fd);
  return 0;
}
