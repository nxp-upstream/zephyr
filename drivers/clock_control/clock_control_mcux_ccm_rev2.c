/*
 * Copyright (c) 2021, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_ccm_rev2
#include <errno.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/imx_ccm_rev2.h>
#include <fsl_clock.h>

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control);

/**
 * defined in fsl_clock.c
 * members:
 * {
 *     bool clockOff;
 *     uint8_t mux;
 *     uint8_t div;
 * }
 */
typedef struct _clock_root_config_t mcux_ccm_sybsys_config_t;

static int mcux_ccm_get_clock_root(clock_control_subsys_t sub_system,
					clock_root_t *clock_root)
{
	uint32_t clock_name = (uint32_t) sub_system;
	uint32_t peripheral, instance;

	peripheral = (clock_name & IMX_CCM_PERIPHERAL_MASK);
	instance = (clock_name & IMX_CCM_INSTANCE_MASK);
	switch (peripheral) {
#ifdef CONFIG_I2C_MCUX_LPI2C
	case IMX_CCM_LPI2C1_CLK:
		*clock_root = kCLOCK_Root_Lpi2c1 + instance;
		break;
#endif

#ifdef CONFIG_SPI_MCUX_LPSPI
	case IMX_CCM_LPSPI1_CLK:
		*clock_root = kCLOCK_Root_Lpspi1 + instance;
		break;
#endif

#ifdef CONFIG_UART_MCUX_LPUART
	case IMX_CCM_LPUART1_CLK:
		*clock_root = kCLOCK_Root_Lpuart1 + instance;
		break;
#endif

#if CONFIG_IMX_USDHC
	case IMX_CCM_USDHC1_CLK:
		*clock_root = kCLOCK_Root_Usdhc1 + instance;
		break;
#endif

#ifdef CONFIG_DMA_MCUX_EDMA
	case IMX_CCM_EDMA_CLK:
		*clock_root = kCLOCK_Root_Bus;
		break;
	case IMX_CCM_EDMA_LPSR_CLK:
		*clock_root = kCLOCK_Root_Bus_Lpsr;
		break;
#endif

#ifdef CONFIG_PWM_MCUX
	case IMX_CCM_PWM_CLK:
		*clock_root = kCLOCK_Root_Bus;
		break;
#endif

#ifdef CONFIG_CAN_MCUX_FLEXCAN
	case IMX_CCM_CAN1_CLK:
		*clock_root = kCLOCK_Root_Can1 + instance;
		break;
#endif

#ifdef CONFIG_COUNTER_MCUX_GPT
	case IMX_CCM_GPT_CLK:
		*clock_root = kCLOCK_Root_Gpt1 + instance;
		break;
#endif

#ifdef CONFIG_I2S_MCUX_SAI
	case IMX_CCM_SAI1_CLK:
		*clock_root =  kCLOCK_Root_Sai1 + instance;
		break;
#endif
#ifdef CONFIG_MCUX_MEDIAMIX_BLK_CTRL
	case IMX_CCM_MEDIA_AXI_CLK:
		*clock_root = kCLOCK_Root_MediaAxi;
		break;
	case IMX_CCM_MEDIA_APB_CLK:
		*clock_root = kCLOCK_Root_MediaApb;
		break;
	case IMX_CCM_MEDIA_DISP_PIX_CLK:
		*clock_root = kCLOCK_Root_MediaDispPix;
		break;
	case IMX_CCM_MEDIA_LDB_CLK:
		*clock_root = kCLOCK_Root_MediaLdb;
		break;
	case IMX_CCM_CAM_PIX_CLK:
		*clock_root = kCLOCK_Root_CamPix;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static int mcux_ccm_on(const struct device *dev,
				  clock_control_subsys_t sub_system)
{
#ifdef CONFIG_SOC_MIMX93_A55
	uint32_t clock_root;
	int rt = 0;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return rt;

	CLOCK_PowerOnRootClock(clock_root);
	return 0;
#else
#warning This function is only tested on i.MX93
	return 0;
#endif // ! CONFIG_SOC_MIMX93_A55
}

static int mcux_ccm_off(const struct device *dev,
				   clock_control_subsys_t sub_system)
{
#ifdef CONFIG_SOC_MIMX93_A55
	uint32_t clock_root;
	int rt = 0;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return rt;

	CLOCK_PowerOffRootClock(clock_root);
	return 0;
#else
#warning This function is only tested on i.MX93
	return 0;
#endif // ! CONFIG_SOC_MIMX93_A55
}

enum clock_control_status mcux_ccm_get_subsys_status(
								 const struct device *dev,
								 clock_control_subsys_t sub_system)
{
#ifdef CONFIG_SOC_MIMX93_A55
	uint32_t clock_root;
	int rt = 0;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return CLOCK_CONTROL_STATUS_UNKNOWN;

	if (0UL == (CCM_CTRL->CLOCK_ROOT[clock_root].CLOCK_ROOT_CONTROL.RW
					 & CCM_CLOCK_ROOT_OFF_MASK))
		return CLOCK_CONTROL_STATUS_ON;
	else
		return CLOCK_CONTROL_STATUS_OFF;
#else
#warning This function is only tested on i.MX93
	return CLOCK_CONTROL_STATUS_UNKNOWN; // FIXME: unsupported !
#endif // ! CONFIG_SOC_MIMX93_A55
}

static int mcux_ccm_get_subsys_rate(const struct device *dev,
					clock_control_subsys_t sub_system,
					uint32_t *rate)
{
	clock_root_t clock_root;
	int rt = 0;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return rt;

#ifdef CONFIG_SOC_MIMX93_A55
	*rate = CLOCK_GetIpFreq(clock_root);
#else /** For i.MX RT11xx */
	*rate = CLOCK_GetRootClockFreq(clock_root);
#endif // ! CONFIG_SOC_MIMX93_A55
	return 0;
}

static int mcux_ccm_set_subsys_rate(const struct device *dev,
					clock_control_subsys_t sub_system,
					uint32_t *rate)
{
#ifdef CONFIG_SOC_MIMX93_A55
	clock_root_t clock_root;
	int rt = 0;
	uint32_t mux, div_h, div_l;
	uint32_t target_rate = *rate;
	clock_name_t clock_name;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return rt;

	mux = CLOCK_GetRootClockMux(clock_root);
	clock_name = CLOCK_GetRootClockSource(clock_root, mux);
	assert(clock_name <= kCLOCK_Ext);
	div_l = g_clockSourceFreq[clock_name] / target_rate;
	div_h = div_l + 1;

	if ((g_clockSourceFreq[clock_name] / div_l - target_rate)
		 < target_rate - g_clockSourceFreq[clock_name] / div_h)
		CLOCK_SetRootClockDiv(clock_root, div_l);
	else
		CLOCK_SetRootClockDiv(clock_root, div_h);

	return 0;

#else
#error This function is only tested on i.MX93
	return -ENOSYS;
#endif // ! CONFIG_SOC_MIMX93_A55
}

static int mcux_ccm_configure_subsys(const struct device *dev,
					  clock_control_subsys_t sub_system,
					  void *data)
{
#ifdef CONFIG_SOC_MIMX93_A55
	clock_root_t clock_root;
	int rt = 0;

	rt = mcux_ccm_get_clock_root(sub_system, &clock_root);
	if(rt != 0)
		return rt;

	mcux_ccm_sybsys_config_t* config = (mcux_ccm_sybsys_config_t*)data;
	CLOCK_SetRootClock(clock_root, config);

	return 0;
#else
#error This function is only tested on i.MX93
	return -ENOSYS;
#endif // ! CONFIG_SOC_MIMX93_A55
}

static int mcux_ccm_init(const struct device *dev)
{
#ifdef CONFIG_SOC_MIMX93_A55
	const fracn_pll_init_t audioPllCfg = {
		.rdiv = 1,
		.mfi = 163,
		.mfn = 84,
		.mfd = 100,
		.odiv = 10,
	};

	const fracn_pll_init_t videoPllCfg = {
		.rdiv = 1,
		.mfi = 175,
		.mfn = 0,
		.mfd = 100,
		.odiv = 10,
	};

	/** PLL_CLKx = (24M / rdiv * (mfi + mfn/mfd) / odiv) */

	CLOCK_PllInit(AUDIOPLL, &audioPllCfg);
	CLOCK_PllInit(VIDEOPLL, &videoPllCfg);

	g_clockSourceFreq[kCLOCK_Osc24M] 			= 24000000U;
	g_clockSourceFreq[kCLOCK_SysPll1]			= 4000000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd0]		= 1000000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd0Div2]	= 500000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd1]		= 800000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd1Div2]	= 400000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd2]		= 625000000U;
	g_clockSourceFreq[kCLOCK_SysPll1Pfd2Div2]	= 312500000U;
	g_clockSourceFreq[kCLOCK_AudioPll1]			= 393216000U;
	g_clockSourceFreq[kCLOCK_AudioPll1Out]		= 393216000U;
	g_clockSourceFreq[kCLOCK_VideoPll1]			= 420000000U;
	g_clockSourceFreq[kCLOCK_VideoPll1Out]		= 420000000U;
#endif
	return 0;
}

#ifdef CONFIG_SOC_MIMX93_A55
static const struct clock_control_driver_api mcux_ccm_driver_api = {
	.on = mcux_ccm_on,
	.off = mcux_ccm_off,
	.get_rate = mcux_ccm_get_subsys_rate,
	.get_status = mcux_ccm_get_subsys_status,
	.set_rate = (clock_control_set)mcux_ccm_set_subsys_rate,
	.configure = mcux_ccm_configure_subsys,
};
#else
static const struct clock_control_driver_api mcux_ccm_driver_api = {
	.on = mcux_ccm_on,
	.off = mcux_ccm_off,
	.get_rate = mcux_ccm_get_subsys_rate,
};
#endif

DEVICE_DT_INST_DEFINE(0,
		    &mcux_ccm_init,
		    NULL,
		    NULL, NULL,
		    PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY,
		    &mcux_ccm_driver_api);
