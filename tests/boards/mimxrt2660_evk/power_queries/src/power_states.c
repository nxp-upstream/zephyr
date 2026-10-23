/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/ztest.h>

#include "stay_awake.h"

#define CPU0_NODE DT_NODELABEL(cpu0)

/* 1 if entry idx of the CPU node's cpu-power-states is state_node. */
#define CPU_POWER_STATE_IS(cpu_node, prop, idx, state_node)                                        \
	DT_SAME_NODE(DT_PHANDLE_BY_IDX(cpu_node, prop, idx), state_node)

/* How many entries of cpu0's cpu-power-states are the node. */
#define CPU0_LISTINGS(node_id)                                                                     \
	(DT_FOREACH_PROP_ELEM_SEP_VARGS(CPU0_NODE, cpu_power_states, CPU_POWER_STATE_IS, (+),      \
					node_id))

#define POLICY_MAY_SELECT(node_label)                                                              \
	(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(node_label)) &&                                      \
	 (CPU0_LISTINGS(DT_NODELABEL(node_label)) == 1))

/*
 * Deep Sleep 3, Power Down and Deep Power Down 1 wake through a system reset. If
 * the policy could pick one, an idle application would reboot without having
 * asked to, so they are entered through pm_state_force() only.
 */
BUILD_ASSERT((DT_NUM_CPU_POWER_STATES(CPU0_NODE) == 3) && POLICY_MAY_SELECT(sleep) &&
		     POLICY_MAY_SELECT(deep_sleep1) && POLICY_MAY_SELECT(deep_sleep2),
	     "The PM policy may only select Sleep, Deep Sleep 1 and Deep Sleep 2");

#define TEST_CPU 0U

/* The substates under which the SoC power code enters the states that wake through a reset. */
#define DEEP_SLEEP3_SUBSTATE_ID      0U
#define POWER_DOWN_SUBSTATE_ID       1U
#define DEEP_POWER_DOWN1_SUBSTATE_ID 0U

struct reset_on_wake_state {
	const char *name;
	enum pm_state state;
	uint8_t substate_id;
};

static const struct reset_on_wake_state reset_on_wake_states[] = {
	{"Deep Sleep 3", PM_STATE_SUSPEND_TO_RAM, DEEP_SLEEP3_SUBSTATE_ID},
	{"Power Down", PM_STATE_SUSPEND_TO_RAM, POWER_DOWN_SUBSTATE_ID},
	{"Deep Power Down 1", PM_STATE_SUSPEND_TO_DISK, DEEP_POWER_DOWN1_SUBSTATE_ID},
};

/* The states are only looked up: forcing one would reset the board under test. */
ZTEST(power_states, test_reset_on_wake_states_can_be_forced)
{
	ARRAY_FOR_EACH_PTR(reset_on_wake_states, expected) {
		zassert_not_null(pm_state_get(TEST_CPU, expected->state, expected->substate_id),
				 "pm_state_force() cannot find %s (state %d, substate %u)",
				 expected->name, expected->state, expected->substate_id);
	}
}

ZTEST_SUITE(power_states, NULL, stay_awake_setup, NULL, NULL, stay_awake_teardown);
