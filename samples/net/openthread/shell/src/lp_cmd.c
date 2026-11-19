/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OpenThread low power CLI addon for Zephyr (MCXW72 SED).
 *
 * This reproduces the FreeRTOS "lp" command behavior using the native Zephyr
 * Power Management policy API.
 *
 * MCXW72 SED mapping:
 *   WFI       -> plain WFI, all clocks running, CLI available
 *   Sleep     -> PM_STATE_SUSPEND_TO_IDLE  (CMC Sleep, ~134 uA)
 *   DeepSleep -> PM_STATE_STANDBY          (CMC Deep Sleep, ~0.18 uA)
 *
 * Power Down / Deep Power Down are not used: they reset the part on wakeup
 * and lose the Thread state (child ID, routing table, MLE session).
 *
 *   PWR_SetLowPowerModeConstraint()     -> pm_policy_state_lock_get()
 *   PWR_ReleaseLowPowerModeConstraint() -> pm_policy_state_lock_put()
 *   xTimerCreate (one-shot)             -> k_work_delayable
 *
 * Semantic note: on FreeRTOS a constraint means "deepest mode allowed". On
 * Zephyr the device is by default allowed to enter every declared power state,
 * so we emulate the "WFI only" default by locking the deeper states at init.
 * The command temporarily releases (unlocks) states up to the requested mode
 * for a given duration, then a delayed work re-locks them so the shell/UART
 * becomes available again.
 */

#include <zephyr/kernel.h>

/*
 * The "lp" command relies on the Zephyr Power Management policy API
 * (pm_policy_state_lock_get/put), which is only available when CONFIG_PM is
 * enabled. On boards/configs without PM this file compiles to nothing so the
 * sample still builds (the equivalent of the FreeRTOS OT_APP_CLI_LOWPOWER_ADDON
 * guard).
 */
#ifdef CONFIG_PM

#include <zephyr/shell/shell.h>
#include <zephyr/pm/policy.h>
#include <stdlib.h>
#include <zephyr/drivers/uart.h>

/* Default low power window duration, matches the FreeRTOS 10s default. */
#define LOWPOWER_DEFAULT_DURATION_MS 10000U

#pragma message "Low power mode compiled with default duration: " STRINGIFY(LOWPOWER_DEFAULT_DURATION_MS) " ms"

/* Low power modes selectable from the command line. */
enum lp_mode {
	LP_MODE_WFI = 0,        /* plain WFI                      (CLI available)   */
	LP_MODE_SLEEP,          /* PM_STATE_SUSPEND_TO_IDLE        (CMC Sleep)       */
	LP_MODE_DEEPSLEEP,      /* PM_STATE_STANDBY                (CMC Deep Sleep)  */
};

/*
 * The states deeper than WFI that we lock/unlock to gate low power.
 *   sleep     -> PM_STATE_SUSPEND_TO_IDLE (CMC Sleep, warm resume, ~134 uA)
 *   deepsleep -> PM_STATE_STANDBY         (CMC Deep Sleep, warm resume, ~0.18 uA)
 */
static const enum pm_state lp_deep_states[] = {
	PM_STATE_SUSPEND_TO_IDLE,
	PM_STATE_STANDBY,
};

/*
 * Per-state guard: true means the state is currently unlocked (allowed).
 * This mirrors the ZBOSS lp_on idempotency guard: a state is only ever
 * lock_put() once from the locked default and only ever lock_get() once from
 * the unlocked state, so the PM policy lock reference count for each state
 * stays balanced no matter how many times the "lp" command is invoked.
 */
static bool lp_state_unlocked[ARRAY_SIZE(lp_deep_states)];

static enum lp_mode current_mode = LP_MODE_WFI;
static uint32_t lp_duration_ms = LOWPOWER_DEFAULT_DURATION_MS;


static void lp_timer_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(lp_timer_work, lp_timer_work_handler);

static void lp_soc_restore(void);
static void lp_soc_prepare(const void *sh, int mode, uint32_t duration_ms);

/*
 * Lock (forbid) all states deeper than WFI. This is the default state and the
 * state we return to when the low power window expires, so that the shell/UART
 * is available to the user.
 *
 * The per-state lp_state_unlocked[] guard makes each lock idempotent: a state
 * is only lock_get() when it is currently unlocked, so repeated calls cannot
 * push the PM policy reference count above its balanced value.
 */
static void lp_lock_deep_states(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(lp_deep_states); i++) {
		if (lp_state_unlocked[i]) {
			pm_policy_state_lock_get(lp_deep_states[i], PM_ALL_SUBSTATES);
			lp_state_unlocked[i] = false;
		}
	}
}

/*
 * Return whether the given state is allowed for the requested low power mode.
 * Each mode unlocks its state and all shallower ones (up to WFI).
 *   sleep     -> unlock SUSPEND_TO_IDLE only
 *   deepsleep -> unlock SUSPEND_TO_IDLE + STANDBY
 */
static bool lp_state_allowed(enum pm_state state, enum lp_mode mode)
{
	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		return mode >= LP_MODE_SLEEP;
	case PM_STATE_STANDBY:
		return mode >= LP_MODE_DEEPSLEEP;
	default:
		return false;
	}
}

/*
 * Apply the requested low power mode by unlocking the states allowed by that
 * mode and (re)locking the others.
 *
 * Each transition is driven by the per-state lp_state_unlocked[] guard rather
 * than by comparing current_mode: a state is only ever lock_put() once from
 * the locked default and only ever lock_get() once from the unlocked state.
 * This mirrors the ZBOSS lp_on idempotency guard and keeps the PM policy lock
 * reference count balanced even if current_mode and the real lock state drift
 * apart (for example after a window timeout re-locks the states and a new
 * "lp" command is issued).
 */
static void lp_configure_mode(enum lp_mode next_mode)
{
	for (size_t i = 0; i < ARRAY_SIZE(lp_deep_states); i++) {
		bool want_unlocked = lp_state_allowed(lp_deep_states[i], next_mode);

		if (want_unlocked && !lp_state_unlocked[i]) {
			/* Release the default lock: allow entering this state. */
			pm_policy_state_lock_put(lp_deep_states[i], PM_ALL_SUBSTATES);
			lp_state_unlocked[i] = true;
		} else if (!want_unlocked && lp_state_unlocked[i]) {
			/* Re-take the lock: forbid this state again. */
			pm_policy_state_lock_get(lp_deep_states[i], PM_ALL_SUBSTATES);
			lp_state_unlocked[i] = false;
		}
	}

	current_mode = next_mode;
}


/*
 * Delayed work handler: the low power window expired, force WFI-only mode so
 * the serial interface becomes available again to the user.
 */
static void lp_timer_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	lp_configure_mode(LP_MODE_WFI);
	lp_soc_restore();
}

static const char *lp_mode_str(enum lp_mode mode)
{
	switch (mode) {
	case LP_MODE_WFI:
		return "WFI";
	case LP_MODE_SLEEP:
		return "Sleep";
	case LP_MODE_DEEPSLEEP:
		return "DeepSleep";
	default:
		return "Unknown";
	}
}

static int lp_parse_mode(const char *arg, enum lp_mode *out)
{
	if (strcmp(arg, "wfi") == 0) {
		*out = LP_MODE_WFI;
	} else if (strcmp(arg, "sleep") == 0) {
		*out = LP_MODE_SLEEP;
	} else if (strcmp(arg, "deepsleep") == 0) {
		*out = LP_MODE_DEEPSLEEP;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int cmd_lp(const struct shell *sh, size_t argc, char **argv)
{
	enum lp_mode next_mode = current_mode;
	bool have_mode = false;
	int i = 1;

	if (argc == 1) {
		shell_print(sh, "Usage: lp [-m <mode>] [-t <ms>]");
		shell_print(sh,
			    "  -m [wfi|sleep|deepsleep] "
			    "Select maximum low power mode");
		shell_print(sh, "  -t <ms> Low power window duration (default: %u ms)",
			    LOWPOWER_DEFAULT_DURATION_MS);
		shell_print(sh, "          Use -t 0 for permanent low power (no auto-return).");
		shell_print(sh, "The selected mode is used as much as possible during the");
		shell_print(sh, "window; after it expires the mode is forced back to WFI so");
		shell_print(sh, "that the shell becomes available again.");
		return 0;
	}

	while (i < argc) {
		if (strcmp(argv[i], "-m") == 0) {
			if (i + 1 >= argc) {
				shell_error(sh, "Missing mode argument");
				return -EINVAL;
			}
			if (lp_parse_mode(argv[i + 1], &next_mode) != 0) {
				shell_error(sh, "Invalid low power mode");
				return -EINVAL;
			}
			have_mode = true;
			i += 2;
		} else if (strcmp(argv[i], "-t") == 0) {
			if (i + 1 >= argc) {
				shell_error(sh, "Missing time argument");
				return -EINVAL;
			}
			lp_duration_ms = strtoul(argv[i + 1], NULL, 10);
			shell_print(sh, "Timer updated to %u ms", lp_duration_ms);
			i += 2;
		} else {
			shell_error(sh, "Invalid argument: %s", argv[i]);
			return -EINVAL;
		}
	}

	if (have_mode) {
		lp_configure_mode(next_mode);
		shell_print(sh, "%s mode selected", lp_mode_str(next_mode));

		if (next_mode == LP_MODE_WFI) {
			/* WFI selected explicitly: cancel any pending window so the
			 * shell stays available. */
			k_work_cancel_delayable(&lp_timer_work);
			/* Restore NBU and UART. */
			lp_soc_restore();
		} else if (lp_duration_ms == 0U) {
			/* Park the NBU so it can deep sleep between polls. */
			lp_soc_prepare(sh, next_mode, 0);
			/* Permanent low power: unlock the deep states and do NOT
			 * schedule a return timer. The PM policy will enter the
			 * deepest allowed state whenever the idle thread runs.
			 * OpenThread drives the sleep/wake cycle via its poll
			 * timer. The only way to recover the shell is a board
			 * reset. */
			k_work_cancel_delayable(&lp_timer_work);
			shell_print(sh, "Permanent %s enabled (reset to recover shell)",
				    lp_mode_str(next_mode));
		} else {
			/* Park the NBU so it can deep sleep during the window. */
			lp_soc_prepare(sh, next_mode, lp_duration_ms);
			/* Start (or restart) the one-shot window. When it expires,
			 * the delayed work forces WFI-only mode so the shell comes
			 * back and the deep states are locked again. */
			k_work_reschedule(&lp_timer_work, K_MSEC(lp_duration_ms));
			shell_print(sh, "Selected mode will be used for the next %u ms",
				    lp_duration_ms);
		}
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(lp, NULL, "OpenThread low power control", cmd_lp, 1, 4);


/*
 * SoC override for the generic "lp" shell command (see
 * samples/net/openthread/shell/src/lp_cmd.c). MCXW7xx exposes two low power
 * modes suitable for a Thread SED (warm resume, state retained):
 *
 *   sleep         -> PM_STATE_SUSPEND_TO_IDLE (CMC Sleep)
 *   deepsleep     -> PM_STATE_STANDBY         (CMC Deep Sleep)
 *
 * The 'mode' argument is an lp_mode value (0=WFI, 1=Sleep, 2=DeepSleep)
 * passed as a plain int so this file needs no sample header.
 */
enum {
	LP_SOC_MODE_WFI = 0,
	LP_SOC_MODE_SLEEP,
	LP_SOC_MODE_DEEPSLEEP,
};

bool lp_soc_map_state(int mode, enum pm_state *state)
{
	switch (mode) {
	case LP_SOC_MODE_SLEEP:
		*state = PM_STATE_SUSPEND_TO_IDLE;
		return true;
	case LP_SOC_MODE_DEEPSLEEP:
		*state = PM_STATE_STANDBY;
		return true;
	default:
		return false;
	}
}

/*
 * Prepare the SoC for the low power window.
 *
 * Following the ZBOSS low-power model, there is no RF_CMC1->RADIO_LP /
 * BLE_WKUP manipulation here: once the deep PM states are unlocked and the
 * NBU/radio messaging unit (RF_IMU0) is armed as a WUU wake source in the SoC
 * Deep Sleep path (see pm_state_set() in soc/.../power.c), the NBU manages its
 * own low power and sleeps on its own when idle. The function body is a no-op
 * kept for symmetry with lp_soc_restore() and the FreeRTOS prepare hook.
 */
static void lp_soc_prepare(const void *sh, int mode, uint32_t duration_ms)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(mode);
	ARG_UNUSED(duration_ms);

	if (mode == LP_SOC_MODE_WFI) {
		return;
	}
}


/*
 * Restore the console/shell LPUART at the end of the low power window.
 *
 * The full CMC Sleep mode gates the LPUART1 clock while the CM33 is asleep, so
 * on the intermediate LPTMR wakes the receiver stops and the Zephyr LPUART
 * driver loses sync with the hardware (its RX interrupt-enable bookkeeping no
 * longer matches the peripheral). Doing the restore through the Zephyr UART API
 * rather than poking the LPUART registers directly via the HAL lets the driver
 * re-apply its own state, so the shell thread is re-signalled and the prompt
 * comes back.
 *
 * This runs once, from lp_timer_work_handler() in the system workqueue thread
 * (IRQs live) after lp_configure_mode(LP_MODE_WFI) has re-locked the deep
 * states, which is the exact equivalent of the FreeRTOS BOARD_ExitLowPowerCb().
 * A disable/enable toggle forces the driver to reprogram the RX interrupt from
 * a known state.
 */
static void lp_soc_restore(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(console)) {
		return;
	}

	uart_irq_rx_disable(console);
	uart_irq_rx_enable(console);
}


/*
 * Establish the default policy as early as possible, before the kernel idle
 * thread can run and let the PM idle policy select a deep state.
 *
 * This MUST run early (PRE_KERNEL_1): the power-state nodes (SUSPEND_TO_IDLE
 * and STANDBY) are "okay" in the dtsi, so they are idle-policy candidates. If
 * we only locked them later, the idle thread could enter a deep state on its
 * own during the boot window. Locking here guarantees every state deeper than
 * WFI is forbidden from the very first idle tick until the user opts in via
 * the "lp" command.
 *
 * pm_policy_state_lock_get() only manipulates atomic reference counters guarded
 * by a spinlock, so it is safe to call at PRE_KERNEL_1 (no kernel dependency).
 */
static int lp_lock_init(void)
{
	/*
	 * Take the default lock unconditionally here: at boot the states are
	 * idle-policy candidates (no lock held yet), so lp_lock_deep_states()
	 * (which is guarded by lp_state_unlocked[]) would be a no-op. This is
	 * the one place that establishes the balanced locked baseline; from
	 * then on lp_configure_mode()/lp_lock_deep_states() keep it balanced.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(lp_deep_states); i++) {
		pm_policy_state_lock_get(lp_deep_states[i], PM_ALL_SUBSTATES);
		lp_state_unlocked[i] = false;
	}
	current_mode = LP_MODE_WFI;

	return 0;
}


SYS_INIT(lp_lock_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#endif /* CONFIG_PM */