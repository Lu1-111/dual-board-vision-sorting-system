/* Direct framebuffer color bars; never touches GPIO. Restores pixels on exit. */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }
int main(int argc, char **argv)
{
    int fd = open("/dev/fb0", O_RDWR);
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) ||
        ioctl(fd, FBIOGET_FSCREENINFO, &f)) { perror("fb info"); return 1; }
    printf("%ux%u bpp=%u stride=%u offsets R=%u G=%u B=%u\n",
           v.xres, v.yres, v.bits_per_pixel, f.line_length,
           v.red.offset, v.green.offset, v.blue.offset);
    fflush(stdout);
    if (argc < 2) { close(fd); return 0; }
    int seconds = atoi(argv[1]);
    if (seconds < 1 || seconds > 180 || v.bits_per_pixel != 24 ||
        v.red.length != 8 || v.green.length != 8 || v.blue.length != 8 ||
        v.red.offset > 16 || v.green.offset > 16 || v.blue.offset > 16 ||
        v.xres < 6 || !v.yres ||
        (uint64_t)(v.xoffset + v.xres) * 3 > f.line_length ||
        (uint64_t)(v.yoffset + v.yres) * f.line_length > f.smem_len) {
        fprintf(stderr, "Unsupported format/geometry/duration\n"); close(fd); return 2;
    }
    unsigned char *mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    unsigned rows = v.yres < 240 ? v.yres : 240;
    size_t size = (size_t)rows * v.xres * 3;
    unsigned char *saved = malloc(size);
    if (!saved) { munmap(mem, f.smem_len); close(fd); return 1; }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGHUP, stop);
    static const unsigned char colors[6][3] = {
        {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}, {0,255,255}, {255,0,255}
    };
    for (unsigned y=0; y<rows; ++y) {
        unsigned char *dst = mem + (size_t)(y+v.yoffset)*f.line_length + v.xoffset*3;
        memcpy(saved + (size_t)y*v.xres*3, dst, v.xres*3);
        for (unsigned x=0; x<v.xres; ++x) {
            const unsigned char *c = colors[(uint64_t)x*6/v.xres];
            uint32_t pixel = ((uint32_t)c[0]<<v.red.offset) |
                             ((uint32_t)c[1]<<v.green.offset) |
                             ((uint32_t)c[2]<<v.blue.offset);
            dst[x*3] = pixel & 255; dst[x*3+1] = (pixel>>8)&255;
            dst[x*3+2] = (pixel>>16)&255;
        }
    }
    puts("DIRECT BARS LEFT TO RIGHT: RED GREEN BLUE YELLOW CYAN MAGENTA"); fflush(stdout);
    for (int i=0; i<seconds && !stopped; ++i) sleep(1);
    for (unsigned y=0; y<rows; ++y)
        memcpy(mem + (size_t)(y+v.yoffset)*f.line_length + v.xoffset*3,
               saved + (size_t)y*v.xres*3, v.xres*3);
    free(saved); munmap(mem, f.smem_len); close(fd);
    puts("RESTORED");
    return 0;
}
