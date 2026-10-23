/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/cpu_freq/cpu_freq.h>
#include <zephyr/cpu_freq/pstate.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include <fsl_power.h>

#include "soc_clock.h"

LOG_MODULE_REGISTER(imxrt266x_cpu_freq, CONFIG_CPU_FREQ_LOG_LEVEL);

/* The cpu-frequency-level values of nxp,rt2k-pmu-pstate, as tokens. */
enum rt2k_freq_level {
	RT2K_FREQ_LOW,
	RT2K_FREQ_NORMAL,
	RT2K_FREQ_HIGH,
};

/*
 * A run mode: the HAL entry, which orders the core voltage and body bias
 * against the clock change, and the clock change it calls back.
 */
struct rt2k_run_mode {
	power_run_mode_t id;
	void (*enter)(power_clock_cb_t clock_cb);
	power_clock_cb_t switch_clocks;
};

static const struct rt2k_run_mode rt2k_run_modes[] = {
	[RT2K_FREQ_LOW] = {
		.id = kPOWER_RunModeLp,
		.enter = POWER_EnterLpRun,
		.switch_clocks = soc_clock_switch_to_lp_run,
	},
	[RT2K_FREQ_NORMAL] = {
		.id = kPOWER_RunModeNormal,
		.enter = POWER_EnterNormalRun,
		.switch_clocks = soc_clock_switch_to_normal_run,
	},
	[RT2K_FREQ_HIGH] = {
		.id = kPOWER_RunModeHp,
		.enter = POWER_EnterHpRun,
		.switch_clocks = soc_clock_switch_to_od_run,
	},
};

struct rt2k_pstate_cfg {
	const struct rt2k_run_mode *run_mode;
};

#define RT2K_FREQ_LEVEL(node_id) _CONCAT(RT2K_FREQ_, DT_STRING_UPPER_TOKEN(node_id, cpu_frequency_level))

#define RT2K_PSTATE_DEFINE(node_id)                                                                \
	static const struct rt2k_pstate_cfg _CONCAT(rt2k_pstate_cfg_, node_id) = {                 \
		.run_mode = &rt2k_run_modes[RT2K_FREQ_LEVEL(node_id)],                             \
	};                                                                                         \
	PSTATE_DT_DEFINE(node_id, &_CONCAT(rt2k_pstate_cfg_, node_id))

DT_FOREACH_CHILD_STATUS_OKAY(DT_PATH(performance_states), RT2K_PSTATE_DEFINE)

/*
 * Serializes a switch requested from a thread with the one the cpu_freq timer
 * requests from its ISR: the HAL decides the voltage ordering from the CPU rate
 * it reads when a switch starts.
 */
static struct k_spinlock rt2k_cpu_freq_lock;

/* HP Run and Low Power Run are only entered from Normal Run. */
static bool rt2k_needs_normal_run_hop(power_run_mode_t from, power_run_mode_t to)
{
	return ((from == kPOWER_RunModeHp) && (to == kPOWER_RunModeLp)) ||
	       ((from == kPOWER_RunModeLp) && (to == kPOWER_RunModeHp));
}

static void rt2k_enter_run_mode(const struct rt2k_run_mode *mode)
{
	mode->enter(mode->switch_clocks);
}

int cpu_freq_pstate_set(const struct pstate *state)
{
	const struct rt2k_pstate_cfg *cfg;
	const struct rt2k_run_mode *target;
	k_spinlock_key_t key;

	if ((state == NULL) || (state->config == NULL)) {
		LOG_ERR("P-state or its configuration is NULL");
		return -EINVAL;
	}

	cfg = state->config;
	target = cfg->run_mode;

	LOG_DBG("Entering run mode %d (threshold %u%%)", (int)target->id, state->load_threshold);

	key = k_spin_lock(&rt2k_cpu_freq_lock);

	if (rt2k_needs_normal_run_hop(POWER_GetCurrentRunMode(), target->id)) {
		rt2k_enter_run_mode(&rt2k_run_modes[RT2K_FREQ_NORMAL]);
	}

	rt2k_enter_run_mode(target);

	k_spin_unlock(&rt2k_cpu_freq_lock, key);

	return 0;
}
