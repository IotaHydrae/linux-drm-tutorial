/*
  fb_pixel_set               Linux Framebuffer RGB565 Pixel Setter
  Usage: fb_pixel_set [fb_device] x y color
  Example: sudo ./tests/fb_pixel_set.out /dev/fb0 64 80 f800
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
  unsigned char *buffer;
  unsigned int color, x, y;
  char *end;
  char *fb_path = "/dev/fb0";
  char *sx, *sy, *scolor;

  if (argc == 4) {
    sx = argv[1];
    sy = argv[2];
    scolor = argv[3];
  } else if (argc == 5) {
    fb_path = argv[1];
    sx = argv[2];
    sy = argv[3];
    scolor = argv[4];
  } else {
    printf("Usage: %s [fb_device] x y color\n", argv[0]);
    return 1;
  }

  x = strtoul(sx, &end, 10);
  if (end == sx || *end != '\0') {
    printf("Invalid x coordinate: %s\n", sx);
    return 1;
  }

  y = strtoul(sy, &end, 10);
  if (end == sy || *end != '\0') {
    printf("Invalid y coordinate: %s\n", sy);
    return 1;
  }

  color = strtoul(scolor, &end, 16);
  if (end == scolor || *end != '\0' || color > 0xffff) {
    printf("Invalid RGB565 color: %s\n", scolor);
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

  if (x >= info.xres || y >= info.yres) {
    printf("Pixel (%u,%u) out of bounds for %ux%u framebuffer\n",
           x, y, info.xres, info.yres);
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

  *((unsigned short *)(buffer + y * info.xres * info.bits_per_pixel / 8 +
                       x * info.bits_per_pixel / 8)) = (unsigned short)color;

  printf("Set pixel (%u,%u) to RGB565 0x%04x\n", x, y, color);

  munmap(buffer, len);
  close(fd);
  return 0;
}
