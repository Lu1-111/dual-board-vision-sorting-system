# Camera ABI and sensor table

These two files are copied without modification from this workspace's
`sdk/mipi_hdmi_preview/driver/mipicam_module.h` and
`sdk/mipi_hdmi_preview/imx415_regs.h`. Their original Anlogic copyright and MIT
SPDX notices are preserved. The sensor table was extracted by the reference
project from `sdk/face_detection.tar.gz`, `utils/media/mipi_camera.cpp`.

The ABI includes the reference driver's driver-owned 1920 x 1080 BGRA DMA
allocation and explicit CPU/device cache synchronization ioctls (5, 6 and 7).
An older driver without those ioctls cannot be used. This application does not
modify, replace or unload the driver, access `/dev/fb0`, or run inference.
