/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debug GPIO for FRDM-MCXW72.
 * Single header for all low power / radio instrumentation signals.
 *
 * Pin mapping:
 *   J2 D13 (pin 8) = PTB0  -> radio RX state LEVEL (HIGH=Receive, LOW=Sleep)
 *   J2 D11 (pin 7) = PTB1  -> PM_STATE_SUSPEND_TO_IDLE (Sleep) pulse
 *   J2 D12 (pin 6) = PTB3  -> PM_STATE_STANDBY (Deep Sleep) pulse
 *   J1 pin 5       = PTA19 -> pm_system_suspend entry/exit pulse
 *   J1 pin 7       = PTA20 -> RxOnWhenIdle LEVEL (HIGH=on-when-idle, LOW=SED)
 */
#ifndef DEBUG_GPIO_MCXW72_H
#define DEBUG_GPIO_MCXW72_H

#ifdef __cplusplus
extern "C" {
#endif

void debug_gpio_init(void);

/* PM_STATE_SUSPEND_TO_IDLE (Sleep) pulse - PTB1 */
void debug_sleep_high(void);
void debug_sleep_low(void);

/* PM_STATE_STANDBY (Deep Sleep) pulse - PTB3 */
void debug_deepsleep_high(void);
void debug_deepsleep_low(void);

/* Radio RX-state LEVEL - PTB0 (HIGH=Receive, LOW=Sleep) */
void debug_rx_state_high(void);
void debug_rx_state_low(void);

/* pm_system_suspend entry/exit pulse - PTA19 */
void debug_pm_high(void);
void debug_pm_low(void);

/* RxOnWhenIdle LEVEL - PTA20 (HIGH=on-when-idle, LOW=SED) */
void debug_rxidle_high(void);
void debug_rxidle_low(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_GPIO_MCXW72_H */
