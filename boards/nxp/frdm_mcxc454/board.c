/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/device.h>
#include <fsl_clock.h>
#include <fsl_reset.h>
#include <soc.h>

/*
 * MCXC454 comes out of reset running MAIN_CLK from FRO12M, which
 * system_MCXC454.c reports as DEFAULT_SYSTEM_CLOCK. This board keeps that
 * operating point: it needs no core LDO level change, no SRAM voltage change
 * and no extra flash read wait states, and FRO_LF_DIV then supplies the
 * LPUART reference clock at the 12 MHz the console baud rate is derived from.
 */
#define BOARD_CORE_CLOCK 12000000U

/* System clock frequency. */
extern uint32_t SystemCoreClock;

void board_early_init_hook(void)
{
	/* FRO12M feeds MAIN_CLK and, through FRO_LF_DIV, the LPUARTs. */
	CLOCK_SetupFRO12MClocking();

	CLOCK_SetClockDiv(kCLOCK_DivAHBCLK, 1U);
	CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);

	/* FRO_LF_DIV = FRO12M / 1 = 12 MHz. */
	CLOCK_SetClockDiv(kCLOCK_DivFRO_LF, 1U);

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(porta))
	RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(portb))
	RESET_ReleasePeripheralReset(kPORT1_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(portc))
	RESET_ReleasePeripheralReset(kPORT2_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(portd))
	RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio0))
	RESET_ReleasePeripheralReset(kGPIO0_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_GateGPIO0);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio1))
	RESET_ReleasePeripheralReset(kGPIO1_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_GateGPIO1);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio2))
	RESET_ReleasePeripheralReset(kGPIO2_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_GateGPIO2);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpio3))
	RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
	CLOCK_EnableClock(kCLOCK_GateGPIO3);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(lpuart0))
	CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1U);
	CLOCK_AttachClk(kFRO_LF_DIV_to_LPUART0);
	/* LPUART0 is held in reset with its bus clock gated after POR. */
	CLOCK_EnableClock(kCLOCK_GateLPUART0);
	RESET_ReleasePeripheralReset(kLPUART0_RST_SHIFT_RSTn);
#endif

	/* Set SystemCoreClock variable. */
	SystemCoreClock = BOARD_CORE_CLOCK;
}
