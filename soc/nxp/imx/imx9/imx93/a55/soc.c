/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>
#include <fsl_common.h>

#if DT_HAS_COMPAT_STATUS_OKAY(nxp_imx93_video_pll)
#define VIDEO_PLL_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nxp_imx93_video_pll)
#endif

static int imx93_video_pll_init(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(nxp_imx93_video_pll)
	const fracn_pll_init_t pll_cfg = {
		.rdiv = DT_PROP(VIDEO_PLL_NODE, rdiv),
		.mfi = DT_PROP(VIDEO_PLL_NODE, mfi),
		.mfn = DT_PROP(VIDEO_PLL_NODE, mfn),
		.mfd = DT_PROP(VIDEO_PLL_NODE, mfd),
		.odiv = DT_PROP(VIDEO_PLL_NODE, odiv),
	};

	uint32_t freq = DT_PROP(VIDEO_PLL_NODE, pll_frequency);

	CLOCK_PllInit(VIDEOPLL, &pll_cfg);
	g_clockSourceFreq[kCLOCK_VideoPll1] = freq;
	g_clockSourceFreq[kCLOCK_VideoPll1Out] = freq;
#endif
	return 0;
}

static int soc_init(void)
{
	int ret = imx93_video_pll_init();

	if (ret) {
		printf("SoC VIDEO PLL init failed");
		return ret;
	}

	return 0;
}
/*
 * Init video pll based on config
 */
SYS_INIT(soc_init, PRE_KERNEL_2, 0);
