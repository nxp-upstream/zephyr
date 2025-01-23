/*
 * Copyright 2024  NXP
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/init.h>
#include <zephyr/device.h>
#include "fsl_power.h"
#include "fsl_clock.h"
#include <soc.h>
#include <fsl_glikey.h>
#include "fsl_iopctl.h"
#include "fsl_reset.h"

/*!< System oscillator settling time in us */
#define SYSOSC_SETTLING_US 220U
/*!< xtal frequency in Hz */
#define XTAL_SYS_CLK_HZ	24000000U

#define SET_UP_FLEXCOMM_CLOCK(x)								\
	do {														\
		CLOCK_AttachClk(kFCCLK0_to_FLEXCOMM##x);				\
		RESET_ClearPeripheralReset(kFC##x##_RST_SHIFT_RSTn);	\
		CLOCK_EnableClock(kCLOCK_LPFlexComm##x);				\
	} while (0)

typedef enum {
	kClockModule_FRO0,
	kClockModule_FRO1,
	kClockModule_FRO2,
	kClockModule_XTAL_OSC,
	kClockModule_OSC32KNP,
	kClockModule_RTC_SS,
	kClockModule_MAIN_PLL0,
	kClockModule_AUDIO_PLL0,
	kClockModule_VDDN_COM_BASE_CLK_SEL,
	kClockModule_VDD2_COMP_BASE_CLK_SEL,
	kClockModule_VDD2_DSP_BASE_CLK_SEL,
	kClockModule_VDD2_COM_BASE_CLK_SEL,
	kClockModule_AUDIO_VDD2_CLK_SEL,
	kClockModule_FCCLK0_CLK_SEL,
	kClockModule_FCCLK1_CLK_SEL,
	kClockModule_FCCLK2_CLK_SEL,
	kClockModule_FCCLK3_CLK_SEL,
	kClockModule_VDD1_SENSE_BASE_CLK_SEL,
	kClockModule_AUDIO_VDD1_CLK_SEL,
	kClockModule_LPOSC_1M_CLK_SEL,
	kClockModule_WAKE32K_CLK_SEL,
	kClockModule_VDD2_MEDIA_BASE_CLK_SEL,
	kClockModule_VDDN_MEDIA_BASE_CLK_SEL,
	kClockModule_LOW_FREQ_CLK_SEL,
	kClockModule_CLK_ROOT_COMPUTE_MAIN_CLK,
	kClockModule_CLK_ROOT_DSP_CLK,
	kClockModule_CLK_ROOT_COMMON_RAM_CLK,
	kClockModule_CLK_ROOT_COMPUTE_TPIU_CLK,
	kClockModule_CLK_ROOT_XSPI0_FCLK,
	kClockModule_CLK_ROOT_XSPI1_FCLK,
	kClockModule_CLK_ROOT_SCT_FCLK,
	kClockModule_CLK_ROOT_UTICK0_FCLK,
	kClockModule_CLK_ROOT_WDT0_FCLK,
	kClockModule_CLK_ROOT_WDT1_FCLK,
	kClockModule_CLK_ROOT_COMPUTE_SYSTICK_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM0_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM1_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM2_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM3_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM4_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM5_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM6_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM7_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM8_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM9_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM10_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM11_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM12_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM13_FCLK,
	kClockModule_CLK_ROOT_SAI012_CLK,
	kClockModule_CLK_ROOT_CTIMER0_FCLK,
	kClockModule_CLK_ROOT_CTIMER1_FCLK,
	kClockModule_CLK_ROOT_CTIMER2_FCLK,
	kClockModule_CLK_ROOT_CTIMER3_FCLK,
	kClockModule_CLK_ROOT_CTIMER4_FCLK,
	kClockModule_CLK_ROOT_I3C01_FCLK,
	kClockModule_CLK_ROOT_I3C01_PCLK,
	kClockModule_CLK_ROOT_COMM2_CLKOUT,
	kClockModule_CLK_ROOT_SENSE_DSP_CLK,
	kClockModule_CLK_ROOT_SAI3_CLK,
	kClockModule_CLK_ROOT_UTICK1_CLK,
	kClockModule_CLK_ROOT_WDT2_FCLK,
	kClockModule_CLK_ROOT_WDT3_FCLK,
	kClockModule_CLK_ROOT_SENSE_SYSTICK_FCLK,
	kClockModule_CLK_ROOT_CTIMER5_FCLK,
	kClockModule_CLK_ROOT_CTIMER6_FCLK,
	kClockModule_CLK_ROOT_CTIMER7_FCLK,
	kClockModule_CLK_ROOT_I3C23_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM17_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM18_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM19_FCLK,
	kClockModule_CLK_ROOT_FLEXCOMM20_FCLK,
	kClockModule_CLK_ROOT_COMMON_VDDN_CLK,
	kClockModule_CLK_ROOT_osc_clk_usb,
	kClockModule_CLK_ROOT_osc_clk_eusb,
	kClockModule_CLK_ROOT_SENSE_MAIN_CLK,
	kClockModule_CLK_ROOT_SENSE_RAM_CLK,
	kClockModule_CLK_ROOT_OSEVENT_FCLK,
	kClockModule_CLK_ROOT_SDADC_FCLK,
	kClockModule_CLK_ROOT_SARADC_FCLK,
	kClockModule_CLK_ROOT_MICFIL_FCLK,
	kClockModule_CLK_ROOT_LPI2C_FCLK,
	kClockModule_CLK_ROOT_SENSE_CLKOUT,
	kClockModule_CLK_ROOT_MEDIA_VDDN_CLK,
	kClockModule_CLK_ROOT_MEDIA_MAIN_CLK,
	kClockModule_CLK_ROOT_XSPI2_FCLK,
	kClockModule_CLK_ROOT_USB_WAKE_CLK,
	kClockModule_CLK_ROOT_eUSB_WAKE_CLK,
	kClockModule_CLK_ROOT_SDIO0_FCLK,
	kClockModule_CLK_ROOT_SDIO1_FCLK,
	kClockModule_CLK_ROOT_DPHY_BIT_CLK,
	kClockModule_CLK_ROOT_DPHY_RX_CLK,
	kClockModule_CLK_ROOT_GPU_FCLK,
	kClockModule_CLK_ROOT_LPSPI14_FCLK,
	kClockModule_CLK_ROOT_LPSPI16_FCLK,
	kClockModule_CLK_ROOT_FLEXIO_CLK,
	kClockModule_CLK_ROOT_DCPIXEL_FCLK,
	kClockModule_MEDIA_MAINCLK_SHUTOFF,
	kClockModule_COMN_MAINCLK_SHUTOFF,
	kClockModule_SENSES_MAINCLK_SHUTOFF,
	kClockModule_SENSEP_MAINCLK_SHUTOFF,
	kClockModule_COMP_MAINCLK_SHUTOFF,
} clock_module_t;

const clock_fro_config_t g_fro0Config_BOARD_BootClockHSRUN = {
	.targetFreq = 325000000,				  /* FRO0 TUNER output clock frequency: 325000000Hz */
	.range = 50,							  /* FRO0 Trim1 delay: 50us */
	.trim1DelayUs = 5,						/* FRO0 Trim1 delay: 5us */
	.trim2DelayUs = 150,					  /* FRO0 Trim2 delay: 150us */
	.refDiv = 1,							  /* The FRO reference divider is 1 */
	.enableInt = 0,						   /* The FRO interrupts are disabled */
	.coarseTrimEn = true,					 /* The coarse value autotrimming is enabled */
};

const clock_main_pll_config_t g_mainPllConfig_clock_init = {
	.main_pll_src = kCLOCK_MainPllOscClk, /* OSC clock */
	.numerator = 0,   /* Numerator of the SYSPLL0 fractional loop divider is 0 */
	.denominator = 1, /* Denominator of the SYSPLL0 fractional loop divider is 1 */
	.main_pll_mult = kCLOCK_MainPllMult22 /* Divide by 22 */
};

const clock_audio_pll_config_t g_audioPllConfig_clock_init =
	{
		.audio_pll_src = kCLOCK_AudioPllOscClk,   /* XTAL OSC clock */
		.numerator = 5040,						/* Audio PLL0 numerator value: 5040 */
		.denominator = 27000,					 /* Audio PLL0 numerator value: 27000 */
		.audio_pll_mult = kCLOCK_AudioPllMult22,  /* Divide by 22 */
		.enableVcoOut = true,					 /* The Audio PLL0 VCO clock output is enabled */
	};

static void BOARD_InitAHBSC(void);

static void BOARD_ClockPreConfig(void)
{
	POWER_DisablePD(kPDRUNCFG_PD_FRO1); /* Make sure FRO1 is enabled. */

	/* Switch to FRO1 for safe configure. */
	CLOCK_AttachClk(kFRO1_DIV1_to_COMPUTE_BASE);
	CLOCK_AttachClk(kCOMPUTE_BASE_to_COMPUTE_MAIN);
	CLOCK_SetClkDiv(kCLOCK_DivCmptMainClk, 1U);
	CLOCK_AttachClk(kFRO1_DIV1_to_RAM);
	CLOCK_SetClkDiv(kCLOCK_DivComputeRamClk, 1U);
	CLOCK_AttachClk(kFRO1_DIV1_to_COMMON_BASE);
	CLOCK_AttachClk(kCOMMON_BASE_to_COMMON_VDDN);
	CLOCK_SetClkDiv(kCLOCK_DivCommonVddnClk, 1U);
}

void BOARD_BootClockHSRUN_InitClockModule(clock_module_t module)
{
	switch(module) {
		case kClockModule_FRO0:
			/* Enable power and ungate the FRO0. */
			POWER_DisablePD(kPDRUNCFG_GATE_FRO0);
			POWER_DisablePD(kPDRUNCFG_PD_FRO0);
			/* Configure FRO clock module in closed loop (autotrimming) mode */
			CLOCK_EnableFroClkFreqCloseLoop(FRO0, &g_fro0Config_BOARD_BootClockHSRUN, kCLOCK_FroDiv1OutEn | kCLOCK_FroDiv2OutEn | kCLOCK_FroDiv3OutEn | kCLOCK_FroDiv6OutEn | kCLOCK_FroDiv8OutEn);
			/* Setup domain specific clock gates */
			CLOCK_EnableFro0ClkForDomain(kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			break;
		case kClockModule_XTAL_OSC:
			/* Configure XTAL oscillator clock module */
			POWER_DisablePD(kPDRUNCFG_PD_SYSXTAL);
			CLOCK_EnableSysOscClk(true, true, 220); /* Enable system OSC */
			CLOCK_SetXtalFreq(XTAL_SYS_CLK_HZ);					/* Sets external XTAL OSC freq */
			break;
		case kClockModule_MAIN_PLL0:
			/* Configure Main PLL0 clock module */
			CLKCTL2->MAINPLL0CTL0 |= CLKCTL2_MAINPLL0CTL0_BYPASS_MASK; /* Bypass Main PLL0 and PFDs outputs */
			CLOCK_InitMainPll(&g_mainPllConfig_clock_init);
			/* Main PLL0 PFD0 output value = Main PLL0 VCO * 18 / 19 */
			CLOCK_InitMainPfd(kCLOCK_Pfd0, 19U);
			CLOCK_EnableMainPllPfdClkForDomain(kCLOCK_Pfd0, kCLOCK_VddnComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Main PLL0 PFD1 output value = Main PLL0 VCO * 18 / 24 */
			CLOCK_InitMainPfd(kCLOCK_Pfd1, 24U);
			CLOCK_EnableMainPllPfdClkForDomain(kCLOCK_Pfd1, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Main PLL0 PFD2 output value = Main PLL0 VCO * 18 / 18 */
			CLOCK_InitMainPfd(kCLOCK_Pfd2, 18U);
			CLOCK_EnableMainPllPfdClkForDomain(kCLOCK_Pfd2, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Main PLL0 PFD3 output value = Main PLL0 VCO * 18 / 19 */
			CLOCK_InitMainPfd(kCLOCK_Pfd3, 19U);
			CLOCK_EnableMainPllPfdClkForDomain(kCLOCK_Pfd3, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			CLKCTL2->MAINPLL0CTL0 &= ~CLKCTL2_MAINPLL0CTL0_BYPASS_MASK; /* Disable bypass of the Main PLL0 and PFDs outputs */
			break;
		case kClockModule_AUDIO_PLL0:
			/* Configure Audio PLL0 clock module */
			CLKCTL2->AUDIOPLL0CTL0 |= CLKCTL2_AUDIOPLL0CTL0_BYPASS_MASK; /* Bypass Audio PLL0 and PFDs outputs */
			CLOCK_InitAudioPll(&g_audioPllConfig_clock_init);
			/* Audio PLL0 PFD0 output value = Audio PLL0 VCO * 18 / 0 */
			CLOCK_InitAudioPfd(kCLOCK_Pfd0, 0U);
			CLOCK_EnableAudioPllPfdClkForDomain(kCLOCK_Pfd0, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Audio PLL0 PFD1 output value = Audio PLL0 VCO * 18 / 24 */
			CLOCK_InitAudioPfd(kCLOCK_Pfd1, 24U);
			CLOCK_EnableAudioPllPfdClkForDomain(kCLOCK_Pfd1, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Audio PLL0 PFD2 output value = Audio PLL0 VCO * 18 / 0 */
			CLOCK_InitAudioPfd(kCLOCK_Pfd2, 0U);
			CLOCK_EnableAudioPllPfdClkForDomain(kCLOCK_Pfd2, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Audio PLL0 PFD3 output value = Audio PLL0 VCO * 18 / 26 */
			CLOCK_InitAudioPfd(kCLOCK_Pfd3, 26U);
			CLOCK_EnableAudioPllPfdClkForDomain(kCLOCK_Pfd3, kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			/* Setup domain specific clock gates of the Audio PLL VCO output */
			CLOCK_EnableAudioPllVcoClkForDomain(kCLOCK_VddnComDomainEnable | kCLOCK_Vdd2ComDomainEnable | kCLOCK_VddnMediaDomainEnable | kCLOCK_Vdd2MediaDomainEnable | kCLOCK_Vdd2DspDomainEnable | kCLOCK_Vdd1SenseDomainEnable | kCLOCK_Vdd2CompDomainEnable);
			CLKCTL2->AUDIOPLL0CTL0 &= ~CLKCTL2_AUDIOPLL0CTL0_BYPASS_MASK; /* Disable bypass of the Audio PLL0 and PFDs outputs */
			break;
		case kClockModule_VDDN_COM_BASE_CLK_SEL:
			/* Switch COMMON_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_COMMON_BASE);
			break;
		case kClockModule_VDD2_COMP_BASE_CLK_SEL:
			/* Switch COMPUTE_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_COMPUTE_BASE);
			break;
		case kClockModule_VDD2_DSP_BASE_CLK_SEL:
			/* Switch DSP_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_DSP_BASE);
			break;
		case kClockModule_VDD2_COM_BASE_CLK_SEL:
			/* Switch COMMON_VDD2_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_COMMON_VDD2_BASE);
			break;
		case kClockModule_LPOSC_1M_CLK_SEL:
			POWER_DisablePD(kPDRUNCFG_PD_LPOSC); /* Enable the LPOSC 1 MHz*/
			break;
		case kClockModule_VDD2_MEDIA_BASE_CLK_SEL:
			/* Switch MEDIA_VDD2_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_MEDIA_VDD2_BASE);
			break;
		case kClockModule_VDDN_MEDIA_BASE_CLK_SEL:
			/* Switch MEDIA_VDDN_BASE to FRO1_DIV1 */
			CLOCK_AttachClk(kFRO1_DIV1_to_MEDIA_VDDN_BASE);
			break;
		case kClockModule_MEDIA_MAINCLK_SHUTOFF:
			POWER_DisablePD(kPDRUNCFG_SHUT_MEDIA_MAINCLK); /* Enable the media_main_clk and media_vddn_clk clocks. */
			break;
		case kClockModule_COMN_MAINCLK_SHUTOFF:
			POWER_DisablePD(kPDRUNCFG_SHUT_COMNN_MAINCLK); /* Enable the COMMON_VDDN_CLK clock. */
			break;
		case kClockModule_SENSES_MAINCLK_SHUTOFF:
			POWER_DisablePD(kPDRUNCFG_SHUT_SENSES_MAINCLK); /* Enable the SENSE_MAIN_CLK_1 clock. */
			break;
		case kClockModule_COMP_MAINCLK_SHUTOFF:
			POWER_DisablePD(kPDRUNCFG_SHUT_COMPT_MAINCLK); /* Enable the COMPUTE_MAIN_CLK clock. */
			break;
		case kClockModule_CLK_ROOT_COMPUTE_MAIN_CLK:
			/* Set COMPUTE_MAIN_CLK divider to value 1 */
			CLOCK_SetClkDiv(kCLOCK_DivCmptMainClk, 1U);
			/* Switch COMPUTE_MAIN_CLK selector to FRO0.FRO_MAX_VDD2_COMP_CLK */
			CLOCK_AttachClk(kFRO0_DIV1_to_COMPUTE_MAIN);
			break;
		case kClockModule_CLK_ROOT_COMMON_RAM_CLK:
			/* Set COMMON_RAM_CLK divider to value 1 */
			CLOCK_SetClkDiv(kCLOCK_DivComputeRamClk, 1U);
			/* Switch COMMON_RAM_CLK selector to FRO0.FRO_MAX_VDD2_COM_CLK */
			CLOCK_AttachClk(kFRO0_DIV1_to_RAM);
			break;
		case kClockModule_CLK_ROOT_COMM2_CLKOUT:
			/* Switch COMM2_CLKOUT selector to FRO0.FRO_MAX_VDD2_COM_CLK */
			CLOCK_AttachClk(kFRO0_DIV1_to_VDD2_CLKOUT);
			/* Set COMM2_CLKOUT divider to value 10 */
			CLOCK_SetClkDiv(kCLOCK_DivClockOut, 10U);
			break;
		case kClockModule_CLK_ROOT_COMMON_VDDN_CLK:
			/* Set COMMON_VDDN_CLK divider to value 1 */
			CLOCK_SetClkDiv(kCLOCK_DivCommonVddnClk, 1U);
			/* Switch COMMON_VDDN_CLK selector to CLKCTL2.baseclk_comn */
			CLOCK_AttachClk(kCOMMON_BASE_to_COMMON_VDDN);
			break;
		case kClockModule_CLK_ROOT_SENSE_CLKOUT:
			/* Switch SENSE_CLKOUT selector to off state. */
			CLOCK_AttachClk(kNONE_to_VDD1_CLKOUT);
			break;
		case kClockModule_CLK_ROOT_MEDIA_VDDN_CLK:
			/* Set MEDIA_VDDN_CLK divider to value 1 */
			CLOCK_SetClkDiv(kCLOCK_DivMediaVddnClk, 1U);
			/* Switch MEDIA_VDDN_CLK selector to FRO0.FRO_MAX_VDDN_MEDIA_CLK */
			CLOCK_AttachClk(kFRO0_DIV1_to_MEDIA_VDDN);
			break;
		case kClockModule_CLK_ROOT_MEDIA_MAIN_CLK:
			/* Set MEDIA_MAIN_CLK divider to value 1 */
			CLOCK_SetClkDiv(kCLOCK_DivMediaMainClk, 1U);
			/* Switch MEDIA_MAIN_CLK selector to FRO0.FRO_MAX_VDD2_MEDIA_CLK */
			CLOCK_AttachClk(kFRO0_DIV1_to_MEDIA_MAIN);
			break;
		default:
			assert(false);
			break;
	}
}

void board_early_init_hook(void)
{
	BOARD_InitAHBSC();

#if CONFIG_SOC_MIMXRT798S_CM33_CPU0

#ifndef CONFIG_IMXRT7XX_CODE_CACHE
	CACHE64_DisableCache(CACHE64_CTRL0);
#endif

	BOARD_ClockPreConfig();

#if CONFIG_FLASH_MCUX_XSPI_XIP
	/* Change to common_base clock(Sourced by FRO1). */
	xspi_clock_safe_config();
#endif

	/* Change power supply for LDO, if using external PMIC supply for VDD1/VDD2, need configure PMIC to change voltage supply. */
	power_regulator_voltage_t ldo = {
		.LDO.vsel0 = 700000U,  /* 700mv, 0.45 V + 12.5 mV * x */
		.LDO.vsel1 = 800000U,  /* 800mv*/
		.LDO.vsel2 = 900000U,  /* 900mv */
		.LDO.vsel3 = 1100000U, /* 1100mv */
	};

	power_lvd_voltage_t lvd = {
		.VDD12.lvl0 = 600000U, /* 600mv */
		.VDD12.lvl1 = 700000U, /* 700mv */
		.VDD12.lvl2 = 800000U, /* 800mv */
		.VDD12.lvl3 = 1000000U, /* 1000mv */
	};

	POWER_ConfigRegulatorSetpoints(kRegulator_Vdd2LDO, &ldo, &lvd);

	POWER_ApplyPD();

	BOARD_BootClockHSRUN_InitClockModule(kClockModule_XTAL_OSC);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_LPOSC_1M_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_FRO0);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_MAIN_PLL0);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_AUDIO_PLL0);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDDN_COM_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDD2_COMP_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDD2_DSP_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDD2_COM_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDD2_MEDIA_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_VDDN_MEDIA_BASE_CLK_SEL);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_CLK_ROOT_COMPUTE_MAIN_CLK);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_CLK_ROOT_COMMON_RAM_CLK);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_CLK_ROOT_COMMON_VDDN_CLK);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_CLK_ROOT_MEDIA_VDDN_CLK);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_CLK_ROOT_MEDIA_MAIN_CLK);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_MEDIA_MAINCLK_SHUTOFF);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_COMN_MAINCLK_SHUTOFF);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_SENSES_MAINCLK_SHUTOFF);
	BOARD_BootClockHSRUN_InitClockModule(kClockModule_COMP_MAINCLK_SHUTOFF);

#if CONFIG_FLASH_MCUX_XSPI_XIP
	/* Call function xspi_setup_clock() to set user configured clock for XSPI. */
	xspi_setup_clock(XSPI0, 3U, 1U); /* Main PLL PDF1 DIV1. */
#endif /* CONFIG_FLASH_MCUX_XSPI_XIP */

#elif CONFIG_SOC_MIMXRT798S_CM33_CPU1
	/* Power up OSC in case it's not enabled. */
	POWER_DisablePD(kPDRUNCFG_PD_SYSXTAL);
	/* Enable system OSC */
	CLOCK_EnableSysOscClk(true, true, SYSOSC_SETTLING_US);
	/* Sets external XTAL OSC freq */
	CLOCK_SetXtalFreq(XTAL_SYS_CLK_HZ);

	CLOCK_AttachClk(kFRO1_DIV3_to_SENSE_BASE);
	CLOCK_SetClkDiv(kCLOCK_DivSenseMainClk, 1);
	CLOCK_AttachClk(kSENSE_BASE_to_SENSE_MAIN);

	POWER_DisablePD(kPDRUNCFG_GATE_FRO2);
	CLOCK_EnableFroClkFreq(FRO2, 300000000U, kCLOCK_FroAllOutEn);

	CLOCK_EnableFro2ClkForDomain(kCLOCK_AllDomainEnable);

	CLOCK_AttachClk(kFRO2_DIV3_to_SENSE_BASE);
	CLOCK_SetClkDiv(kCLOCK_DivSenseMainClk, 1);
	CLOCK_AttachClk(kSENSE_BASE_to_SENSE_MAIN);
#endif /* CONFIG_SOC_MIMXRT798S_CM33_CPU0 */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(iocon), okay)
	RESET_ClearPeripheralReset(kIOPCTL0_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_Iopctl0);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(iocon1), okay)
	RESET_ClearPeripheralReset(kIOPCTL1_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_Iopctl1);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(iocon2), okay)
	RESET_ClearPeripheralReset(kIOPCTL2_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_Iopctl2);
#endif

#ifdef CONFIG_BOARD_MIMXRT700_EVK_MIMXRT798S_CM33_CPU0
	CLOCK_AttachClk(kOSC_CLK_to_FCCLK0);
	CLOCK_SetClkDiv(kCLOCK_DivFcclk0Clk, 1U);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm0), okay)
	SET_UP_FLEXCOMM_CLOCK(0);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm1), okay)
	SET_UP_FLEXCOMM_CLOCK(1);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm2), okay)
	SET_UP_FLEXCOMM_CLOCK(2);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm3), okay)
	SET_UP_FLEXCOMM_CLOCK(3);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm4), okay)
	SET_UP_FLEXCOMM_CLOCK(4);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm5), okay)
	SET_UP_FLEXCOMM_CLOCK(5);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm6), okay)
	SET_UP_FLEXCOMM_CLOCK(6);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm7), okay)
	SET_UP_FLEXCOMM_CLOCK(7);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm8), okay)
	SET_UP_FLEXCOMM_CLOCK(8);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm9), okay)
	SET_UP_FLEXCOMM_CLOCK(9);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm10), okay)
	SET_UP_FLEXCOMM_CLOCK(10);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm11), okay)
	SET_UP_FLEXCOMM_CLOCK(11);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm12), okay)
	SET_UP_FLEXCOMM_CLOCK(12);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm13), okay)
	SET_UP_FLEXCOMM_CLOCK(13);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(lpspi14), okay)
	CLOCK_EnableClock(kCLOCK_LPSpi14);
	RESET_ClearPeripheralReset(kLPSPI14_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(lpi2c15), okay)
	CLOCK_EnableClock(kCLOCK_LPI2c15);
	RESET_ClearPeripheralReset(kLPI2C15_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(lpspi16), okay)
	CLOCK_AttachClk(kFRO0_DIV1_to_LPSPI16);
	CLOCK_SetClkDiv(kCLOCK_DivLpspi16Clk, 1U);
	CLOCK_EnableClock(kCLOCK_LPSpi16);
	RESET_ClearPeripheralReset(kLPSPI16_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm17), okay)
	CLOCK_AttachClk(kSENSE_BASE_to_FLEXCOMM17);
	CLOCK_SetClkDiv(kCLOCK_DivLPFlexComm17Clk, 4U);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm18), okay)
	CLOCK_AttachClk(kSENSE_BASE_to_FLEXCOMM18);
	CLOCK_SetClkDiv(kCLOCK_DivLPFlexComm18Clk, 4U);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm19), okay)
	CLOCK_AttachClk(kSENSE_BASE_to_FLEXCOMM19);
	CLOCK_SetClkDiv(kCLOCK_DivLPFlexComm19Clk, 4U);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(flexcomm20), okay)
	CLOCK_AttachClk(kSENSE_BASE_to_FLEXCOMM20);
	CLOCK_SetClkDiv(kCLOCK_DivLPFlexComm20Clk, 4U);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio0), okay)
	CLOCK_EnableClock(kCLOCK_Gpio0);
	RESET_ClearPeripheralReset(kGPIO0_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
	CLOCK_EnableClock(kCLOCK_Gpio1);
	RESET_ClearPeripheralReset(kGPIO1_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio2), okay)
	CLOCK_EnableClock(kCLOCK_Gpio2);
	RESET_ClearPeripheralReset(kGPIO2_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio3), okay)
	CLOCK_EnableClock(kCLOCK_Gpio3);
	RESET_ClearPeripheralReset(kGPIO3_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio4), okay)
	CLOCK_EnableClock(kCLOCK_Gpio4);
	RESET_ClearPeripheralReset(kGPIO4_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio5), okay)
	CLOCK_EnableClock(kCLOCK_Gpio5);
	RESET_ClearPeripheralReset(kGPIO5_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio6), okay)
	CLOCK_EnableClock(kCLOCK_Gpio6);
	RESET_ClearPeripheralReset(kGPIO6_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio7), okay)
	CLOCK_EnableClock(kCLOCK_Gpio7);
	RESET_ClearPeripheralReset(kGPIO7_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio8), okay)
	CLOCK_EnableClock(kCLOCK_Gpio8);
	RESET_ClearPeripheralReset(kGPIO8_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio9), okay)
	CLOCK_EnableClock(kCLOCK_Gpio9);
	RESET_ClearPeripheralReset(kGPIO9_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio10), okay)
	CLOCK_EnableClock(kCLOCK_Gpio10);
	RESET_ClearPeripheralReset(kGPIO10_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(lcdif), nxp_dcnano_lcdif, okay) && CONFIG_DISPLAY
	/* Assert LCDIF reset. */
	RESET_SetPeripheralReset(kLCDIF_RST_SHIFT_RSTn);

	/* Disable media main and LCDIF power down. */
	POWER_DisablePD(kPDRUNCFG_SHUT_MEDIA_MAINCLK);
	POWER_DisablePD(kPDRUNCFG_APD_LCDIF);
	POWER_DisablePD(kPDRUNCFG_PPD_LCDIF);

	/* Apply power down configuration. */
	POWER_ApplyPD();

	CLOCK_AttachClk(kMAIN_PLL_PFD2_to_LCDIF);
	/* Note- pixel clock follows formula
	 * (height  VSW  VFP  VBP) * (width  HSW  HFP  HBP) * frame rate.
	 * this means the clock divider will vary depending on
	 * the attached display.
	 *
	 * The root clock used here is the main PLL (PLL PFD2).
	 */
	CLOCK_SetClkDiv(
		kCLOCK_DivLcdifClk,
		(CLOCK_GetMainPfdFreq(kCLOCK_Pfd2) /
		  DT_PROP(DT_CHILD(DT_NODELABEL(lcdif), display_timings), clock_frequency)));

	CLOCK_EnableClock(kCLOCK_Lcdif);

	/* Clear LCDIF reset. */
	RESET_ClearPeripheralReset(kLCDIF_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(lcdif), nxp_dcnano_lcdif_dbi, okay)
	/* Assert LCDIF reset. */
	RESET_SetPeripheralReset(kLCDIF_RST_SHIFT_RSTn);

	/* Disable media main and LCDIF power down. */
	POWER_DisablePD(kPDRUNCFG_SHUT_MEDIA_MAINCLK);
	POWER_DisablePD(kPDRUNCFG_APD_LCDIF);
	POWER_DisablePD(kPDRUNCFG_PPD_LCDIF);

	/* Apply power down configuration. */
	POWER_ApplyPD();

	/* Calculate the divider for MEDIA MAIN clock source main pll pfd2. */
	CLOCK_InitMainPfd(kCLOCK_Pfd2, (uint64_t)CLOCK_GetMainPllFreq() * 18UL / DT_PROP(DT_NODELABEL(lcdif), clock_frequency));
	CLOCK_SetClkDiv(kCLOCK_DivMediaMainClk, 1U);
	CLOCK_AttachClk(kMAIN_PLL_PFD2_to_MEDIA_MAIN);

	CLOCK_EnableClock(kCLOCK_Lcdif);

	/* Clear LCDIF reset. */
	RESET_ClearPeripheralReset(kLCDIF_RST_SHIFT_RSTn);
#endif

	// BOARD_Init16bitsPsRam(XSPI2);
	// BOARD_InitPsRamPins_Xspi2();
}

static void GlikeyWriteEnable(GLIKEY_Type *base, uint8_t idx)
{
	(void)GLIKEY_SyncReset(base);

	(void)GLIKEY_StartEnable(base, idx);
	(void)GLIKEY_ContinueEnable(base, GLIKEY_CODEWORD_STEP1);
	(void)GLIKEY_ContinueEnable(base, GLIKEY_CODEWORD_STEP2);
	(void)GLIKEY_ContinueEnable(base, GLIKEY_CODEWORD_STEP3);
	(void)GLIKEY_ContinueEnable(base, GLIKEY_CODEWORD_STEP_EN);
}

static void GlikeyClearConfig(GLIKEY_Type *base)
{
	(void)GLIKEY_SyncReset(base);
}

/* Disable the secure check for AHBSC and enable periperhals/sram access for masters */
static void BOARD_InitAHBSC(void)
{
#if defined(CONFIG_SOC_MIMXRT798S_CM33_CPU0)
	GlikeyWriteEnable(GLIKEY0, 1U);
	AHBSC0->MISC_CTRL_DP_REG = 0x000086aa;
	/* AHBSC0 MISC_CTRL_REG, disable Privilege & Secure checking. */
	AHBSC0->MISC_CTRL_REG = 0x000086aa;

	GlikeyWriteEnable(GLIKEY0, 7U);
	/* Enable arbiter0 accessing SRAM */
	AHBSC0->COMPUTE_ARB0RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC0->SENSE_ARB0RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC0->MEDIA_ARB0RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC0->NPU_ARB0RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC0->HIFI4_ARB0RAM_ACCESS_ENABLE = 0x3FFFFFFF;
#endif

	GlikeyWriteEnable(GLIKEY1, 1U);
	AHBSC3->MISC_CTRL_DP_REG = 0x000086aa;
	/* AHBSC3 MISC_CTRL_REG, disable Privilege & Secure checking.*/
	AHBSC3->MISC_CTRL_REG = 0x000086aa;

	GlikeyWriteEnable(GLIKEY1, 9U);
	/* Enable arbiter1 accessing SRAM */
	AHBSC3->COMPUTE_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC3->SENSE_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC3->MEDIA_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC3->NPU_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC3->HIFI4_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;
	AHBSC3->HIFI1_ARB1RAM_ACCESS_ENABLE = 0x3FFFFFFF;

	GlikeyWriteEnable(GLIKEY1, 8U);
	/* Access enable for COMPUTE domain masters to common APB peripherals.*/
	AHBSC3->COMPUTE_APB_PERIPHERAL_ACCESS_ENABLE = 0xffffffff;
	AHBSC3->SENSE_APB_PERIPHERAL_ACCESS_ENABLE = 0xffffffff;
	GlikeyWriteEnable(GLIKEY1, 7U);
	AHBSC3->COMPUTE_AIPS_PERIPHERAL_ACCESS_ENABLE = 0xffffffff;
	AHBSC3->SENSE_AIPS_PERIPHERAL_ACCESS_ENABLE = 0xffffffff;

	GlikeyWriteEnable(GLIKEY2, 1U);
	/*Disable secure and secure privilege checking. */
	AHBSC4->MISC_CTRL_DP_REG = 0x000086aa;
	AHBSC4->MISC_CTRL_REG = 0x000086aa;

#if defined(CONFIG_SOC_MIMXRT798S_CM33_CPU0)
	GlikeyClearConfig(GLIKEY0);
#endif
	GlikeyClearConfig(GLIKEY1);
	GlikeyClearConfig(GLIKEY2);
}
