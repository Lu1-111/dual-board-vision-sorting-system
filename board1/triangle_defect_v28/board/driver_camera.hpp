#pragma once

#include <opencv2/core.hpp>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "triangle_mipicam_ioctl.h"
#include "triangle_imx415_regs.h"

// Uses the same IMX415 configuration and driver-owned storage as preview.c.
// The kernel retains the DMA allocation after userspace closes the device.
class DriverCamera {
    int camera_fd_ = -1;
    int i2c_fd_ = -1;
    void* pixels_ = MAP_FAILED;
    triangle_mipicam_frame_buffer_t buffer_{};
    bool failed_ = false;

    [[noreturn]] static void systemError(const std::string& operation) {
        const int error = errno;
        throw std::runtime_error(operation + ": " + std::strerror(error));
    }

    static void delayMs(unsigned milliseconds) {
        struct timespec delay{};
        delay.tv_sec = milliseconds / 1000;
        delay.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
        while (::nanosleep(&delay, &delay) != 0) {
            if (errno != EINTR) systemError("IMX415 configuration delay");
        }
    }

    uint8_t readSensor(uint16_t reg) {
        uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
        uint8_t value = 0;
        struct i2c_msg messages[] = {{0x1a, 0, 2, address}, {0x1a, I2C_M_RD, 1, &value}};
        struct i2c_rdwr_ioctl_data operation{messages, 2};
        const int result = ::ioctl(i2c_fd_, I2C_RDWR, &operation);
        if (result < 0) systemError("IMX415 register read");
        if (result != 2) throw std::runtime_error("IMX415 register read was incomplete");
        return value;
    }

    void writeSensor(uint16_t reg, uint8_t value) {
        uint8_t bytes[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg), value};
        struct i2c_msg message{0x1a, 0, 3, bytes};
        struct i2c_rdwr_ioctl_data operation{&message, 1};
        const int result = ::ioctl(i2c_fd_, I2C_RDWR, &operation);
        if (result < 0) systemError("IMX415 register write");
        if (result != 1) throw std::runtime_error("IMX415 register write was incomplete");
        const uint8_t readback = readSensor(reg);
        if (readback != value) {
            char detail[100];
            std::snprintf(detail, sizeof(detail), "IMX415 register %04x: wrote %02x, read %02x",
                          static_cast<unsigned>(reg), static_cast<unsigned>(value),
                          static_cast<unsigned>(readback));
            throw std::runtime_error(detail);
        }
    }

    void writeCamera(uint32_t offset, uint32_t value) {
        triangle_mipicam_rw_info_t reg{offset, value};
        if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_WRITE_REG, &reg) != 0)
            systemError("MIPI capture register write");
    }

    void cleanup() noexcept {
        if (i2c_fd_ >= 0) {
            // Best effort, exactly as preview.c: sensor standby is not a DMA abort.
            uint8_t bytes[] = {0x30, 0x00, 0x01};
            struct i2c_msg message{0x1a, 0, 3, bytes};
            struct i2c_rdwr_ioctl_data operation{&message, 1};
            ::ioctl(i2c_fd_, I2C_RDWR, &operation);
            ::close(i2c_fd_);
            i2c_fd_ = -1;
        }
        if (pixels_ != MAP_FAILED) {
            ::munmap(pixels_, buffer_.size);
            pixels_ = MAP_FAILED;
        }
        if (camera_fd_ >= 0) {
            ::close(camera_fd_);
            camera_fd_ = -1;
        }
        // Never free the driver-owned frame allocation or unload its module.
    }

public:
    explicit DriverCamera(const std::string& path = "/dev/mipicam") {
        try {
            camera_fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (camera_fd_ < 0) systemError("Open MIPI camera " + path);
            triangle_mipicam_reg_info_t registers{};
            if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_GET_REGISTER_INFO, &registers) != 0)
                systemError("Read MIPI register information");
            if (registers.addr != 0x80300000ULL || registers.size < 12)
                throw std::runtime_error("Unexpected board 1 MIPI control register address or size");
            if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_GET_FRAME_BUFFER, &buffer_) != 0)
                systemError("Get driver-owned MIPI frame buffer");
            if (buffer_.size != 1920U * 1080U * 4U || buffer_.phys_addr > 0xffffffffULL)
                throw std::runtime_error("Unexpected MIPI frame buffer size or DMA address");
            pixels_ = ::mmap(nullptr, buffer_.size, PROT_READ | PROT_WRITE, MAP_SHARED, camera_fd_, 0);
            if (pixels_ == MAP_FAILED) systemError("Map MIPI frame buffer");
            i2c_fd_ = ::open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
            if (i2c_fd_ < 0) systemError("Open IMX415 i2c-0");
            (void)readSensor(0x3000);
            for (const auto& reg : triangle_sensor_regs) {
                writeSensor(reg.addr, reg.value);
                if (reg.addr == 0x3000 && reg.value == 0) delayMs(18);
                if (reg.addr == 0x3002 && reg.value == 0) delayMs(320);
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~DriverCamera() { cleanup(); }
    DriverCamera(const DriverCamera&) = delete;
    DriverCamera& operator=(const DriverCamera&) = delete;
    DriverCamera(DriverCamera&&) = delete;
    DriverCamera& operator=(DriverCamera&&) = delete;

    int width() const noexcept { return 1920; }
    int height() const noexcept { return 1080; }
    const unsigned char* data() const noexcept {
        return static_cast<const unsigned char*>(pixels_);
    }

    // Borrowed BGRA view: valid until the next capture or destruction.
    // On a failure, leave the DMA storage with the driver and reject all retries.
    cv::Mat capture() {
        if (failed_) throw std::runtime_error("MIPI camera is faulted; capture will not be restarted");
        try {
            if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_SYNC_FRAME_DEVICE, 0) != 0)
                systemError("Synchronize MIPI frame for DMA");
            writeCamera(0, static_cast<uint32_t>(buffer_.phys_addr));
            // The ioctl pins the module until the capture IRQ; direct MMIO bypasses this.
            writeCamera(4, 1);
            triangle_mipicam_intr_info_t interrupt{};
            interrupt.timeout = 1500;
            if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_WAIT_INTERRUPT, &interrupt) != 0)
                systemError("Wait for MIPI frame completion");
            if (interrupt.intr_flag == 0)
                throw std::runtime_error("MIPI frame completion returned without an interrupt");
            if (::ioctl(camera_fd_, TRIANGLE_MIPICAM_IOCTL_SYNC_FRAME_CPU, 0) != 0)
                systemError("Synchronize MIPI frame for CPU");
            return cv::Mat(height(), width(), CV_8UC4, pixels_);
        } catch (...) {
            failed_ = true;
            throw;
        }
    }
};
