/*
 * Copyright (c) 2023, Anlogic Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "al_dmacahb_hal.h"
#include "alfsbl_secure.h"
#include "alfsbl_boot.h"
#include "al_hal.h"
#ifdef HAVE_QSPIPS_DRIVER
#include "al_spi_nor.h"

static AlSpiNor SpiNor = { 0 };


AlSpiController SpiControllerList[] =
{
    {AL_CONTROLLER_PS_QSPI, 0, AL_SPI_CONTROLLER_DUAL | AL_SPI_CONTROLLER_QUAD, NULL,
        (SpiNorInitOp)AlSpiController_PsQspiInit,
        (SpiNorExecOp)AlSpiController_PsQspiExecOp},
};

const AL_U32 SpiControllerListSize = sizeof(SpiControllerList) / sizeof(SpiControllerList[0]);

AL_U32 AlFsbl_QspiInit(void)
{
    AL_U32 Ret;
    Ret = AlSpiNor_Init(&SpiNor, 0, 0);
    if (Ret != AL_OK) {
        return Ret;
    }

#ifdef QSPI_XIP_THROUTH_CSU_DMA
    AL_QSPI_HalStruct *Handle = (AL_QSPI_HalStruct *)SpiNor.Controller->HalHandle;
    if (SpiNor.AddrBytes == 3) {
        Ret = AlQspi_Dev_XipAddr24InitForDMA(&Handle->Dev);
    } else {
        Ret = AlQspi_Dev_XipAddr32Init(&Handle->Dev);
    }
#endif

    return Ret;
}

AL_U32 AlFsbl_QspiCopy(PTRSIZE SrcAddress, PTRSIZE DestAddress, AL_U32 Length, SecureInfo *pSecureInfo)
{
    AL_U32 Ret = 0;

#ifdef QSPI_XIP_THROUTH_CSU_DMA
    AL_U16 RecvSize;

    AL_LOG(AL_LOG_LEVEL_DEBUG, "xip mode\r\n");
    if(pSecureInfo != NULL) {
        pSecureInfo->InputAddr  = SrcAddress + QSPI_XIP_BASEADDR;
        pSecureInfo->OutputAddr = DestAddress;
        pSecureInfo->DataLength = Length;

        Ret = AlFsbl_DecHash(pSecureInfo);
    }
    else {
        Ret = AlFsbl_CsuDmaCopy(
                SrcAddress + QSPI_XIP_BASEADDR,
                DestAddress,
                Length,
                CSUDMA_DST_INCR | CSUDMA_SRC_INCR);
    }
    AlCache_FlushDcacheAll();

#else

    AL_LOG(AL_LOG_LEVEL_DEBUG, "regular mode\r\n");
    Ret = AlSpiNor_Read(&SpiNor, SrcAddress, Length, (AL_U8 *)DestAddress);

#endif

    AlCache_FlushDcacheAll();

    if(Ret != AL_OK) {
        AL_LOG(AL_LOG_LEVEL_ERROR, "AlNor read byte error\r\n");
        Ret = Ret | (ALFSBL_BOOTMODE_QSPI24 << 16);
    }

    AL_LOG(AL_LOG_LEVEL_DEBUG, "Qspi data copy done\r\n");

    return Ret;
}

AL_U32 AlFsbl_QspiRelease(void)
{
    return 0;
}

AL_U32 AlFsbl_QspiXipInit(void)
{
    AL_QSPI_HalStruct *Handle = (AL_QSPI_HalStruct *)SpiNor.Controller->HalHandle;
    SpiNor.SetWrap64(&SpiNor);
    return AlQspi_Dev_XipAddr24Init(&Handle->Dev, SpiNor.FlashInfo->Id);
}

#endif
