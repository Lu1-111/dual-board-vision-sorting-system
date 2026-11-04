#pragma once
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <string>

class FramebufferDisplay {
    int fd_ = -1;
    void* memory_ = MAP_FAILED;
    fb_var_screeninfo variable_{};
    fb_fix_screeninfo fixed_{};
public:
    explicit FramebufferDisplay(const std::string& path) {
        if (path.empty()) return;
        fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) throw std::runtime_error("Cannot open framebuffer: " + path);
        if (ioctl(fd_, FBIOGET_VSCREENINFO, &variable_) || ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) ||
            variable_.bits_per_pixel != 24 || variable_.red.offset != 16 || variable_.green.offset != 8 ||
            variable_.blue.offset != 0 || variable_.red.length != 8 || variable_.green.length != 8 ||
            variable_.blue.length != 8 || variable_.xoffset || variable_.yoffset ||
            variable_.xres == 0 || variable_.yres == 0 ||
            fixed_.line_length < variable_.xres * 3 ||
            fixed_.smem_len < static_cast<size_t>(fixed_.line_length) * variable_.yres) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Expected a packed 24-bit BGR framebuffer");
        }
        memory_ = mmap(nullptr, fixed_.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (memory_ == MAP_FAILED) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Cannot map framebuffer");
        }
    }
    FramebufferDisplay(const FramebufferDisplay&) = delete;
    FramebufferDisplay& operator=(const FramebufferDisplay&) = delete;
    ~FramebufferDisplay() {
        if (memory_ != MAP_FAILED) munmap(memory_, fixed_.smem_len);
        if (fd_ >= 0) ::close(fd_);
    }
    bool enabled() const { return fd_ >= 0; }
    cv::Mat directBgr(int width, int height) {
        if (!enabled() || width != int(variable_.xres) || height != int(variable_.yres)) return {};
        return cv::Mat(height, width, CV_8UC3, memory_, fixed_.line_length);
    }
    void show(const cv::Mat& bgr) {
        if (!enabled()) return;
        if (bgr.type() != CV_8UC3) throw std::runtime_error("Display expects BGR pixels");
        // Keep boxes and video in the same geometry, including on a different-size display.
        cv::Mat canvas;
        if (bgr.cols == static_cast<int>(variable_.xres) && bgr.rows == static_cast<int>(variable_.yres)) canvas = bgr;
        else {
            const double ratio = std::min(double(variable_.xres) / bgr.cols, double(variable_.yres) / bgr.rows);
            cv::Mat resized;
            cv::resize(bgr, resized, cv::Size(std::max(1, int(bgr.cols * ratio)), std::max(1, int(bgr.rows * ratio))));
            canvas = cv::Mat(variable_.yres, variable_.xres, CV_8UC3, cv::Scalar());
            resized.copyTo(canvas(cv::Rect((canvas.cols-resized.cols)/2, (canvas.rows-resized.rows)/2, resized.cols, resized.rows)));
        }
        for (int y = 0; y < canvas.rows; ++y)
            std::memcpy(static_cast<unsigned char*>(memory_) + y * fixed_.line_length, canvas.ptr(y), canvas.cols * 3);
    }
};
