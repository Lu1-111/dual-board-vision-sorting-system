/****************************************************************
 * Copyright (c) 2023 Anlogic, Inc.  All rights reserved.
 * SPDX-License-Identifier: MIT
 ****************************************************************/

#ifdef __linux__
#ifndef TRIANGLE_MIPICAM_IOCTL_H
#define TRIANGLE_MIPICAM_IOCTL_H
#include <linux/fs.h>
#include <linux/types.h>
#ifndef __KERNEL__
#include <stdint.h>
#endif // !__KERNEL__

#define TRIANGLE_MIPICAM_IOCTL_MAGIC 'S'

#define TRIANGLE_MIPICAM_IOCTL_WAIT_INTERRUPT _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 0)
#define TRIANGLE_MIPICAM_IOCTL_GET_REGISTER_INFO _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 1)
#define TRIANGLE_MIPICAM_IOCTL_WRITE_REG _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 2)
#define TRIANGLE_MIPICAM_IOCTL_READ_REG _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 3)
#define TRIANGLE_MIPICAM_IOCTL_CLEAR_IRQ_REG_OFFSET _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 4)
#define TRIANGLE_MIPICAM_IOCTL_GET_FRAME_BUFFER _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 5)
#define TRIANGLE_MIPICAM_IOCTL_SYNC_FRAME_CPU _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 6)
#define TRIANGLE_MIPICAM_IOCTL_SYNC_FRAME_DEVICE _IO(TRIANGLE_MIPICAM_IOCTL_MAGIC, 7)

typedef struct {
    uint64_t phys_addr;
    uint32_t size;
    uint32_t reserved;
} triangle_mipicam_frame_buffer_t;

typedef struct __triangle_mipicam_intr_info {
	uint32_t timeout; // unit: ms
	uint32_t intr_flag;
	uint32_t work_status;
} triangle_mipicam_intr_info_t;

typedef struct __triangle_mipicam_rw_info_t {
	uint32_t addr;
	uint32_t data;
} triangle_mipicam_rw_info_t;

typedef struct __triangle_mipicam_reg_info_t {
	uint64_t addr;
	uint64_t size;
} triangle_mipicam_reg_info_t;

typedef struct triangle_mipicam_drv_buffer_t {
	unsigned int size;
	unsigned long phys_addr;
	void *virt_addr; /* virtual kernel space address */
} triangle_mipicam_drv_buffer_t;

#endif // !TRIANGLE_MIPICAM_IOCTL_H
#endif
