#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: fb_snapshot output.jpg\n";return 2;}
    const int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
    if(fd<0){perror("fb0");return 1;}
    fb_var_screeninfo v{};fb_fix_screeninfo f{};
    if(ioctl(fd,FBIOGET_VSCREENINFO,&v)||ioctl(fd,FBIOGET_FSCREENINFO,&f)||
       v.bits_per_pixel!=24||v.red.offset!=16||v.green.offset!=8||v.blue.offset!=0||
       v.xoffset||v.yoffset||!v.xres||!v.yres||f.line_length<v.xres*3||
       f.smem_len<static_cast<size_t>(f.line_length)*v.yres){close(fd);return 1;}
    void* pixels=mmap(nullptr,f.smem_len,PROT_READ,MAP_SHARED,fd,0);
    if(pixels==MAP_FAILED){perror("mmap");close(fd);return 1;}
    int result=0;
    try {
        cv::Mat view(v.yres,v.xres,CV_8UC3,pixels,f.line_length),small;
        const cv::Mat frozen=view.clone();
        cv::resize(frozen,small,cv::Size(1280,cvRound(1280.*v.yres/v.xres)),0,0,cv::INTER_AREA);
        if(!cv::imwrite(argv[1],small,{cv::IMWRITE_JPEG_QUALITY,80}))result=1;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    munmap(pixels,f.smem_len);close(fd);return result;
}
