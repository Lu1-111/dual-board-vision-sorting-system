/*
 * Copyright (c) 2023, Anlogic Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "alfsbl_hw.h"
#include "alfsbl_handoff.h"
#include "alfsbl_config.h"
#include "al_reg_io.h"
#include "al_utils_def.h"
#include "al_hal.h"



extern AL_VOID AlMpu_DisableAll(AL_VOID);
extern AL_VOID AlGicv3_CpuIfDisable(AL_U32 ProcNum);

volatile uint32_t RegVal_RAW_HIS0;
volatile uint32_t RegVal_RAW_HIS1;


#if ((!defined ALFSBL_WDT_EXCLUDE) && defined (HAVE_WDTPS_DRIVER))
extern AL_WDT_HalStruct *alfsbl_wdt0;
#endif

static AL_VOID AlFsbl_SetResetVector(AL_U32 CoreId, AL_U64 VectorBase)
{
    if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_RPU) {
        AL_REG32_WRITE(SYSCTRL_S_RPU_RESET_VECTOR_H, VectorBase >> 32);
        AL_REG32_WRITE(SYSCTRL_S_RPU_RESET_VECTOR_L, VectorBase & 0xffffffff);
    }
    else if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_APU0) {
        AL_REG32_WRITE(SYSCTRL_S_APU0_RESET_VECTOR_H, VectorBase >> 34);
        AL_REG32_WRITE(SYSCTRL_S_APU0_RESET_VECTOR_L, (VectorBase >> 2) & 0xffffffff);
    }
    else if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_APU1) {
        AL_REG32_WRITE(SYSCTRL_S_APU1_RESET_VECTOR_H, VectorBase >> 34);
        AL_REG32_WRITE(SYSCTRL_S_APU1_RESET_VECTOR_L, (VectorBase >> 2) & 0xffffffff);
    }
}

static AL_VOID AlFsbl_ResetCoreThenRelease(AL_U32 CoreId)
{
    if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_RPU) {
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 10, 1);  /// release level reset
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 8, 0);   /// trigger pulse reset
    }
    else if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_APU0) {
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 4, 1);   /// release level reset
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 0, 0);   /// trigger pulse reset
    }
    else if(CoreId == ALIH_PH_ATTRIB_DEST_CPU_APU1) {
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 5, 1);   /// release level reset
        AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 1, 0);      /// tringger pulse reset
    }
}

void __attribute__((noinline)) AlFsbl_HandoffExit(uint64_t HandoffAddress)
{
#ifdef __riscv
    AlCache_DisableDCache();
    AlCache_DisableICache();
    AlCore_DisableBPU();
    ISB();
    __asm__ __volatile__("jr %[src]"::[src]"r"(HandoffAddress));
#else
    __asm__ __volatile__("mov x30, %0"::"r"(HandoffAddress):"x30");/* move the destination address into x30 register */
    __asm__ __volatile__("br x30":::"x30");
#endif
}


uint32_t AlFsbl_Handoff(const AlFsblInfo *FsblInstancePtr)
{
    uint32_t Status = ALFSBL_SUCCESS;
    uint32_t HandoffIdx;
    uint32_t RunningCpu;
    uint32_t CpuSettings;
    uint64_t HandoffAddress;
    uint32_t ResetReasonValue = 0;
    AL_U32 HandoffSelf = 0;
    AL_U64 HandoffSelfAddr = 0;

    RunningCpu = FsblInstancePtr->ProcessorID;

    AL_LOG(AL_LOG_LEVEL_INFO, "Mark FSBL is completed...\r\n");
    AL_REG32_WRITE(SYSCTRL_S_FSBL_ERR_CODE, ALFSBL_COMPLETED);

    /// disable pcap to restore pcap-pl isolation
    AL_REG32_WRITE(CSU_PCAP_ENABLE, 0);
#if ((!defined ALFSBL_WDT_EXCLUDE) && defined (HAVE_WDTPS_DRIVER))
    /// disable wdt
    AL_LOG(AL_LOG_LEVEL_INFO, "wdt disable\r\n");
    AlWdt_ll_Pause(alfsbl_wdt0->BaseAddr, AL_FUNC_ENABLE);
    AlWdt_ll_Enable(alfsbl_wdt0->BaseAddr, AL_FUNC_DISABLE);
#endif

    /// init gp, hp, fahb, apu-acp ports between ps and pl
    Soc_PsPlInit();

    /// clear reset reason
    ResetReasonValue = AL_REG32_READ(CRP_RST_REASON);
    AL_REG32_WRITE(CRP_RST_REASON, ResetReasonValue);  /// write 1 clear

    RegVal_RAW_HIS0 = AL_REG32_READ(SYSCTRL_S_RAW_HIS0);
    AL_REG32_WRITE(SYSCTRL_S_RAW_HIS0, RegVal_RAW_HIS0);
    RegVal_RAW_HIS1 = AL_REG32_READ(SYSCTRL_S_RAW_HIS1);
    AL_REG32_WRITE(SYSCTRL_S_RAW_HIS1, RegVal_RAW_HIS1);

#if defined __aarch64__ || defined __arm__
    AlIntr_SetLocalInterrupt(AL_FUNC_DISABLE);
    AlCache_DisableMmu();
    AlGicv3_CpuIfDisable(0);
#endif

    if(FsblInstancePtr->PrimaryBootDevice == ALFSBL_BOOTMODE_JTAG) {
        while(1) {
            __asm__ __volatile__("wfi");
        }
    }

    for(HandoffIdx = 0; HandoffIdx < FsblInstancePtr->HandoffCpuNum; HandoffIdx++) {
        CpuSettings = FsblInstancePtr->HandoffValues[HandoffIdx].CpuSettings;
        HandoffAddress = FsblInstancePtr->HandoffValues[HandoffIdx].HandoffAddress;
        if(RunningCpu != CpuSettings) {
            /// handoff to a different cpu
            /// update reset vector
            /// soft reset the handoff target cpu, pulse reset
            AlFsbl_SetResetVector(CpuSettings, HandoffAddress);
            AlFsbl_ResetCoreThenRelease(CpuSettings);
        } else {
            HandoffSelf = 1;
            HandoffSelfAddr = HandoffAddress;
        }
    }

    if (HandoffSelf) {
        AlFsbl_SetResetVector(RunningCpu, HandoffSelfAddr);
        #if (defined __aarch64__) && (MPU_PROTECT_MODE != 0)
        AlMpu_DisableAll();
        #endif
        AlFsbl_HandoffExit(HandoffSelfAddr);
    }

    return Status;
}
