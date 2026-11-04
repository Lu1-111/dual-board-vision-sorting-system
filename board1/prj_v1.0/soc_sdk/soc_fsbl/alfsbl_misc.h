/*
 * Copyright (c) 2023, Anlogic Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __AL_ALFSBL_MISC_H_
#define __AL_ALFSBL_MISC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "diskio.h"
#include "alfsbl_boot.h"

// ref to alfsbl_boot.h: Boot Modes Definition
#define ALFSBL_SD_DRV_NUM_0     ALFSBL_BOOTMODE_SD      // mmc0 sd
#define ALFSBL_SD_DRV_NUM_1     ALFSBL_BOOTMODE_EMMC    // mmc0 emmc
#define ALFSBL_SD_DRV_NUM_2     ALFSBL_BOOTMODE_EMMC1   // mmc1 emmc

#define ALFSBL_NUM_IN_FILE_NAME 0x6U

typedef struct {
    uint32_t drv_num;
    const char *prefix;
    const char *bootfile;
} DriveConfig;

void *AlFsbl_MemCpy(void * DestPtr, const void * SrcPtr, uint32_t Len);

void delay_us(unsigned int us);

uint32_t endian_convert(uint32_t data);

void AlFsbl_MakeSdFileName(char *FileName, uint32_t MultiBootReg, uint32_t DrvNum);

void print_time_stamp(void);

void AlFsbl_PrintErrorMessage(uint32_t ErrorCode);

#ifdef __cplusplus
}
#endif

#endif /* AL_ALFSBL_MISC_H */
