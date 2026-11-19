/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debug GPIO for FRDM-MCXW72.
 * Single place for all low power / radio instrumentation signals wired to
 * the Arduino headers so they can be captured with a logic analyzer.
 *
 * Pin mapping:
 *   J2 D13 (pin 8) = GPIOB pin 0 (PTB0)  -> radio RX state LEVEL
 *                                            HIGH = otPlatRadioReceive (NBU armed/active)
 *                                            LOW  = otPlatRadioSleep   (NBU allowed idle)
 *   J2 D11 (pin 7) = GPIOB pin 1 (PTB1)  -> PM_STATE_SUSPEND_TO_IDLE (Sleep) pulse
 *   J2 D12 (pin 6) = GPIOB pin 3 (PTB3)  -> PM_STATE_STANDBY (Deep Sleep) pulse
 *   J1 pin 5       = GPIOA pin 19 (PTA19) -> pm_system_suspend entry/exit pulse
 *   J1 pin 7       = GPIOA pin 20 (PTA20) -> RxOnWhenIdle LEVEL
 *                                            HIGH = radio on when idle
 *                                            LOW  = SED mode (RxOnWhenIdle=false)
 */
#include <zephyr/drivers/gpio.h>
#include <fsl_port.h>
#include "debug_gpio_mcxw72.h"

#define DEBUG_GPIOA_NODE    DT_NODELABEL(gpioa)
#define DEBUG_GPIOB_NODE    DT_NODELABEL(gpiob)

/* GPIOB signals */
#define DEBUG_PIN_SLEEP     1   /* J2 D12 pin 6 - PTB1 */
#define DEBUG_PIN_DEEPSLEEP 3   /* J2 D11 pin 7 - PTB3 */
#define DEBUG_PIN_RX_STATE  0   /* J2 D13 pin 8 - PTB0 */

/* GPIOA signals */
#define DEBUG_PIN_PM        19  /* J1 pin 5 - PTA19 */
#define DEBUG_PIN_RXIDLE    20  /* J1 pin 7 - PTA20 */

static const struct device *dbg_gpioa_dev;
static const struct device *dbg_gpiob_dev;
static bool dbg_gpio_ready;

void debug_gpio_init(void)
{
	dbg_gpioa_dev = DEVICE_DT_GET(DEBUG_GPIOA_NODE);
	dbg_gpiob_dev = DEVICE_DT_GET(DEBUG_GPIOB_NODE);
	if (!device_is_ready(dbg_gpioa_dev) || !device_is_ready(dbg_gpiob_dev)) {
		return;
	}

	/* GPIOB: PTB0, PTB1, PTB3 */
	PORT_SetPinMux(PORTB, 0U, kPORT_MuxAsGpio);
	PORT_SetPinMux(PORTB, 1U, kPORT_MuxAsGpio);
	PORT_SetPinMux(PORTB, 3U, kPORT_MuxAsGpio);
	gpio_pin_configure(dbg_gpiob_dev, DEBUG_PIN_RX_STATE, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dbg_gpiob_dev, DEBUG_PIN_SLEEP, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dbg_gpiob_dev, DEBUG_PIN_DEEPSLEEP, GPIO_OUTPUT_INACTIVE);

	/* GPIOA: PTA19, PTA20 */
	PORT_SetPinMux(PORTA, 19U, kPORT_MuxAsGpio);
	PORT_SetPinMux(PORTA, 20U, kPORT_MuxAsGpio);
	gpio_pin_configure(dbg_gpioa_dev, DEBUG_PIN_PM, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dbg_gpioa_dev, DEBUG_PIN_RXIDLE, GPIO_OUTPUT_INACTIVE);

	dbg_gpio_ready = true;
}

/* PM_STATE_SUSPEND_TO_IDLE (Sleep) pulse - PTB1 */
void debug_sleep_high(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_SLEEP, 1); }
}

void debug_sleep_low(void)
{
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_SLEEP, 0); }
}

/* PM_STATE_STANDBY (Deep Sleep) pulse - PTB3 */
void debug_deepsleep_high(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_DEEPSLEEP, 1); }
}

void debug_deepsleep_low(void)
{
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_DEEPSLEEP, 0); }
}

/*
 * Radio RX-state LEVEL signal - PTB0 (J2 D13).
 * HIGH when the radio enters RECEIVE (otPlatRadioReceive), LOW when it enters
 * SLEEP (otPlatRadioSleep). The high-time width measures how long the NBU is
 * kept armed per poll cycle; its period reveals whether OT sleeps every poll
 * (~5 s) or only at child timeout (~125 s).
 */
void debug_rx_state_high(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_RX_STATE, 1); }
}

void debug_rx_state_low(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpiob_dev, DEBUG_PIN_RX_STATE, 0); }
}

/* pm_system_suspend entry/exit pulse - PTA19 */
void debug_pm_high(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpioa_dev, DEBUG_PIN_PM, 1); }
}

void debug_pm_low(void)
{
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpioa_dev, DEBUG_PIN_PM, 0); }
}

/*
 * RxOnWhenIdle LEVEL signal - PTA20 (J1 pin 7).
 * Driven from otPlatRadioSetRxOnWhenIdle(): HIGH when OT allows the radio to
 * stay on while idle, LOW when OT switches to SED mode (RxOnWhenIdle=false).
 */
void debug_rxidle_high(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpioa_dev, DEBUG_PIN_RXIDLE, 1); }
}

void debug_rxidle_low(void)
{
	if (!dbg_gpio_ready) { debug_gpio_init(); }
	if (dbg_gpio_ready) { gpio_pin_set(dbg_gpioa_dev, DEBUG_PIN_RXIDLE, 0); }
}
