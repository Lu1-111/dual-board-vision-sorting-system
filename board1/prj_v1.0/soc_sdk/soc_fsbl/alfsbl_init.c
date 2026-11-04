/*
 * Copyright (c) 2023, Anlogic Inc. and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "alfsbl_hw.h"
#include "alfsbl_init.h"
#include "alfsbl_config.h"
#include "al_hal.h"


static uint32_t AlFsbl_GetResetReason(void);
static uint32_t AlFsbl_SystemInit(AlFsblInfo *FsblInstancePtr);
static uint32_t AlFsbl_ProcessorInit(AlFsblInfo *FsblInstancePtr);
static uint32_t AlFsbl_TcmInit(AlFsblInfo *FsblInstancePtr);
static uint32_t AlFsbl_PmuReset(AlFsblInfo *FsblInstancePtr);
#ifdef ALFSBL_PJTAG_ENABLE
static uint32_t AlFsbl_SwitchToPjtag(void);
#endif
#ifndef ALFSBL_PMU_EXCLUDE
static uint32_t AlFsbl_PmuInit(AlFsblInfo *FsblInstancePtr);
#endif
static uint32_t AlFsbl_ValidateResetReason(void);

#if ((!defined ALFSBL_WDT_EXCLUDE) && defined (HAVE_WDTPS_DRIVER))
void AlWdt_Hal_FsblEventHandler();
static uint32_t AlFsbl_WdtInit(void);
static AL_WDT_InitStruct FSBL_WDT_Init = {
	.ResetPuaseLength = WDT_RPL_PCLK_CYCLES_8,
	.ResponseMode     = WDT_INTR_MODE,
	.TimeOutValue     = WDT_TIMEOUT_PERIOD_2G_CLOCKS,
};
AL_WDT_HalStruct *alfsbl_wdt0;
#endif


uint32_t AlFsbl_Initialize(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = ALFSBL_SUCCESS;
	FsblInstancePtr->ResetReason = AlFsbl_GetResetReason();

	/// system init: peripherals, watch dog
	Status = AlFsbl_SystemInit(FsblInstancePtr);
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}

	AlFsbl_PmuReset(FsblInstancePtr);

	/// clear pending interrupt
	AlIntr_ClearAllPending();

	/// processor init
	/// to get running cpu ID and its running status
	Status = AlFsbl_ProcessorInit(FsblInstancePtr);
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}

#ifdef ALFSBL_PJTAG_ENABLE
	/// switch xpu to pjtag for ds debug mode
	Status = AlFsbl_SwitchToPjtag();
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}

	/// loop to wait for pjtag connection
	while(1) {
		AL_LOG(AL_LOG_LEVEL_INFO, "waiting for pjtag connection...\r\n");
		AlSys_MDelay(1000);
	}
#endif


    AL_U32 PmuHisSts = AL_REG32_READ(SYSCTRL_S_RAW_HIS0);
    if (AL_REG32_READ(CRP_RST_REASON) & 0x000001FF) {
        AL_LOG(AL_LOG_LEVEL_WARNING, "Read RAW_HIS0 before PMU init: 0x%x\r\n", PmuHisSts);
    }

#ifndef ALFSBL_PMU_EXCLUDE
	Status = AlFsbl_PmuInit(FsblInstancePtr);
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}
#endif

	/// tcm ecc init if required
	Status = AlFsbl_TcmInit(FsblInstancePtr);
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}

	/// reset reason validation
	Status = AlFsbl_ValidateResetReason();
	if(Status != ALFSBL_SUCCESS) {
		goto END;
	}

#if ((!defined ALFSBL_WDT_EXCLUDE) && defined (HAVE_WDTPS_DRIVER))
	/// init wdt
	Status = AlFsbl_WdtInit();
#endif

	/// fsbl info data init
	FsblInstancePtr->HandoffCpuNum = 0;

END:
	return Status;
}


static uint32_t AlFsbl_GetResetReason(void)
{
	uint32_t Ret;

	if((AL_REG32_READ(SYSCTRL_S_GLOBAL_SRSTN)) & SYSCTRL_S_GLOBAL_SRSTN_MSK_PSONLY) {
		AL_LOG(AL_LOG_LEVEL_INFO, "PS only reset\r\n");
		Ret = FSBL_PS_ONLY_RESET;
	}
	else {
		AL_LOG(AL_LOG_LEVEL_INFO, "System reset\r\n");
		Ret = FSBL_SYSTEM_RESET;
	}

	return Ret;
}


static uint32_t AlFsbl_SystemInit(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = 0;

	// reset pl
	if(FsblInstancePtr->ResetReason == FSBL_SYSTEM_RESET) {
		AL_REG32_SET_BIT(SYSCTRL_S_GLOBAL_SRSTN, 8, 0);
		AL_REG32_SET_BIT(SYSCTRL_S_GLOBAL_SRSTN, 8, 1);
	}

	/// close pl-ps bus connections, hp and gpm bus
	AL_REG32_SET_BITS(CRP_SRST_CTRL2, 0, 2, 0);
	AL_REG32_SET_BITS(CRP_SRST_CTRL2, 4, 2, 0);

	/// close pl-ps bus connections, fahb and gps bus
	AL_REG32_SET_BITS(SYSCTRL_NS_PLS_PROT, 0, 2, 3);

	/// close apu acp bus connections
	AL_REG32_SET_BITS(CRP_SRST_CTRL0, 8, 1, 0);
	AL_REG32_SET_BITS(APU_CTRL_AINACTS, 0, 1, 1);

	Status = ALFSBL_SUCCESS;

	return Status;
}


static uint32_t AlFsbl_ProcessorInit(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = ALFSBL_SUCCESS;

	/// a temporary solution
#if __riscv
	FsblInstancePtr->ProcessorID = ALIH_PH_ATTRIB_DEST_CPU_RPU;
#else
	FsblInstancePtr->ProcessorID = ALIH_PH_ATTRIB_DEST_CPU_APU0;	//[MODIFY]:2
#endif
	return Status;
}


static uint32_t AlFsbl_PmuReset(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = ALFSBL_SUCCESS;
	AL_LOG(AL_LOG_LEVEL_INFO, "PMU Error Config Init\r\n");
	uint32_t val = 0;

	val = AL_REG32_READ(SYSCTRL_S_ERR_HW_EN0_SET);
	AL_REG32_WRITE(SYSCTRL_S_ERR_HW_EN0_CLR, val);

	val = AL_REG32_READ(SYSCTRL_S_INT_EN0_SET);
	AL_REG32_WRITE(SYSCTRL_S_INT_EN0_CLR, val);

	val = AL_REG32_READ(SYSCTRL_S_ERR_HW_EN1_SET);
	AL_REG32_WRITE(SYSCTRL_S_ERR_HW_EN1_CLR, val);

	val = AL_REG32_READ(SYSCTRL_S_INT_EN1_SET);
	AL_REG32_WRITE(SYSCTRL_S_INT_EN1_CLR, val);

	val = AL_REG32_READ(SYSCTRL_S_RAW_HIS1);
	AL_REG32_WRITE(SYSCTRL_S_RAW_HIS1, val);

	return Status;
}

#ifdef ALFSBL_PJTAG_ENABLE
static uint32_t AlFsbl_SwitchToPjtag(void)
{
	AL_LOG(AL_LOG_LEVEL_INFO, "Switch to PJTAG\r\n");

	/// switch xpu to pjtag
#ifdef __riscv
	AL_REG32_SET_BITS(SYSCTRL_S_JTAG_CTRL, 4, 2, 0x1);
#else
	// AL_REG32_WRITE(SYSCTRL_S_APU1_RESET_VECTOR_H, 0x6103ffd8 >> 34);
	AL_REG32_WRITE(SYSCTRL_S_APU1_RESET_VECTOR_H, 0x0U);
	AL_REG32_WRITE(SYSCTRL_S_APU1_RESET_VECTOR_L, (0x6103ffd8 >> 2) & 0xffffffff);
	AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 5, 1);   /// release level reset
	AL_REG32_SET_BIT(SYSCTRL_S_XPU_SRST, 1, 0);      /// tringger pulse reset
	AL_REG32_SET_BITS(SYSCTRL_S_JTAG_CTRL, 4, 2, 0x3);
#endif

	return ALFSBL_SUCCESS;
}
#endif

#ifndef ALFSBL_PMU_EXCLUDE
static uint32_t AlFsbl_PmuInit(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = ALFSBL_SUCCESS;
	AL_LOG(AL_LOG_LEVEL_INFO, "PMU Error Config Init\r\n");
	uint32_t val = 0;

	val = AL_REG32_READ(SYSCTRL_S_ERR_HW_EN0_SET)   |
			      SYSCTRL_S_ERR_HW0_MSK_BUS_TIMEOUT |
				  SYSCTRL_S_ERR_HW0_MSK_WDT0        |
				  SYSCTRL_S_ERR_HW0_MSK_OCM_ECC;
	AL_REG32_WRITE(SYSCTRL_S_ERR_HW_EN0_SET, val);

	val = AL_REG32_READ(SYSCTRL_S_INT_EN0_SET)      |
			      SYSCTRL_S_ERR_HW0_MSK_BUS_TIMEOUT |
				  SYSCTRL_S_ERR_HW0_MSK_WDT0        |
				  SYSCTRL_S_ERR_HW0_MSK_OCM_ECC;
	AL_REG32_WRITE(SYSCTRL_S_INT_EN0_SET, val);


	if((AL_REG32_READ(CRP_CLK_SEL) & CRP_CLK_SEL_MSK_SLOW_SEL) == CRP_CLK_SEL_MSK_SLOW_SEL) {
		/// pll bypassed
	}
	else {
		/// pll enabled
#if 0
		val = AL_REG32_READ(SYSCTRL_S_ERR_HW_EN1_SET) |
				SYSCTRL_S_ERR_HW1_MSK_PLL2_LOCK |
				SYSCTRL_S_ERR_HW1_MSK_PLL1_LOCK |
				SYSCTRL_S_ERR_HW1_MSK_PLL0_LOCK;
		AL_REG32_WRITE(SYSCTRL_S_ERR_HW_EN1_SET, val);

		val = AL_REG32_READ(SYSCTRL_S_INT_EN1_SET) |
				SYSCTRL_S_ERR_HW1_MSK_PLL2_LOCK |
				SYSCTRL_S_ERR_HW1_MSK_PLL1_LOCK |
				SYSCTRL_S_ERR_HW1_MSK_PLL0_LOCK;
		AL_REG32_WRITE(SYSCTRL_S_INT_EN1_SET, val);
#endif
	}

	return Status;
}
#endif

static uint32_t AlFsbl_TcmInit(AlFsblInfo *FsblInstancePtr)
{
	uint32_t Status = ALFSBL_SUCCESS;
	/// todo

	return Status;
}

#if ((!defined ALFSBL_WDT_EXCLUDE) && defined (HAVE_WDTPS_DRIVER))
void AlWdt_Hal_FsblEventHandler()
{
	AL_LOG(AL_LOG_LEVEL_DEBUG, "wdt int\r\n");
	AlWdt_ll_ClearIntr(alfsbl_wdt0->BaseAddr);
	return;
}

static uint32_t AlFsbl_WdtInit(void)
{
	uint32_t Status = ALFSBL_SUCCESS;
	Status = AlWdt_Hal_Init(&alfsbl_wdt0, 0, &FSBL_WDT_Init, AlWdt_Hal_FsblEventHandler);
	if(Status != ALFSBL_SUCCESS) {
		AL_LOG(AL_LOG_LEVEL_ERROR, "WDT initialize error: 0x%08x\r\n", Status);
		return ALFSBL_ERROR_WDT_INIT_ERR;
	}

	return ALFSBL_SUCCESS;
}
#endif


static uint32_t AlFsbl_ValidateResetReason(void)
{
	uint32_t Status = ALFSBL_SUCCESS;
	uint32_t FsblStatus;
	uint32_t ResetReasonValue;
	uint32_t RawHis0Value;
	uint32_t RawHis1Value;


	/// get fsbl status
	FsblStatus = AL_REG32_READ(SYSCTRL_S_FSBL_ERR_CODE);

	/// get reset reason
	ResetReasonValue = AL_REG32_READ(CRP_RST_REASON);
	RawHis0Value     = AL_REG32_READ(SYSCTRL_S_RAW_HIS0);
	RawHis1Value     = AL_REG32_READ(SYSCTRL_S_RAW_HIS1);

	/// print history record of reset reason and pmu status
	AL_LOG(AL_LOG_LEVEL_INFO, "history reset reason: %08x\r\n", ResetReasonValue);
	AL_LOG(AL_LOG_LEVEL_INFO, "history pmu status 0: %08x\r\n", RawHis0Value);
	AL_LOG(AL_LOG_LEVEL_INFO, "history pmu status 1: %08x\r\n", RawHis1Value);

    //get axi monitor debug state
	AL_LOG(AL_LOG_LEVEL_INFO, "------------------AXI-MONITOR------------------\r\n");
	AL_LOG(AL_LOG_LEVEL_INFO, "debug timeout flag: [4:0] - aw, w, b, ar, r\r\n");
	AL_LOG(AL_LOG_LEVEL_INFO, "ddr_s0   : 0x%x\r\n", AL_REG32_READ(0xf8440000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "ddr_s1   : 0x%x\r\n", AL_REG32_READ(0xf8440400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "ddr_s2   : 0x%x\r\n", AL_REG32_READ(0xf8440800 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "ddr_s3   : 0x%x\r\n", AL_REG32_READ(0xf8440c00 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "sx2x_m0  : 0x%x\r\n", AL_REG32_READ(0xf8441000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "sx2x_m1  : 0x%x\r\n", AL_REG32_READ(0xf8441400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "ocm_s2   : 0x%x\r\n", AL_REG32_READ(0xf8441800 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "sh_m2    : 0x%x\r\n", AL_REG32_READ(0xf8441c00 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "smc      : 0x%x\r\n", AL_REG32_READ(0xf8442000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "dmacx    : 0x%x\r\n", AL_REG32_READ(0xf8442400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "gp_s0    : 0x%x\r\n", AL_REG32_READ(0xf8444000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "gp_s1    : 0x%x\r\n", AL_REG32_READ(0xf8444400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "hp_s0    : 0x%x\r\n", AL_REG32_READ(0xf8444800 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "hp_s1    : 0x%x\r\n", AL_REG32_READ(0xf8444c00 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "apu      : 0x%x\r\n", AL_REG32_READ(0xf8445000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "jpu      : 0x%x\r\n", AL_REG32_READ(0xf8445400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "main_s6  : 0x%x\r\n", AL_REG32_READ(0xf8445800 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "main_m6  : 0x%x\r\n", AL_REG32_READ(0xf8445c00 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "main_s1  : 0x%x\r\n", AL_REG32_READ(0xf8446000 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "main_m0  : 0x%x\r\n", AL_REG32_READ(0xf8446400 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "acp      : 0x%x\r\n", AL_REG32_READ(0xf8446800 + 0xac));
	AL_LOG(AL_LOG_LEVEL_INFO, "-----------------------------------------------\r\n");

	if((ResetReasonValue & CRP_RST_REASON_MSK_SWDT0)   |
	   (ResetReasonValue & CRP_RST_REASON_MSK_SWDT1)   |
	   (ResetReasonValue & CRP_RST_REASON_MSK_SWDT2)) {
		if(FsblStatus == ALFSBL_RUNNING) {
			Status = ALFSBL_ERR_SYS_WDT_RESET;
			goto END;
		}
	}
	else if(ResetReasonValue & CRP_RST_REASON_MSK_PMU_ERR) {
		if(FsblStatus == ALFSBL_RUNNING) {
			Status = ALFSBL_ERR_PMU_ERR_RESET;
			goto END;
		}
	}
	if(FsblStatus != ALFSBL_RUNNING) {
		AL_LOG(AL_LOG_LEVEL_INFO, "mark fsbl is running...\r\n");
		AL_REG32_WRITE(SYSCTRL_S_FSBL_ERR_CODE, ALFSBL_RUNNING);
	}

	/// clear reset reason
	AL_REG32_WRITE(CRP_RST_REASON, ResetReasonValue);
	Status = ALFSBL_SUCCESS;

END:
	return Status;
}



