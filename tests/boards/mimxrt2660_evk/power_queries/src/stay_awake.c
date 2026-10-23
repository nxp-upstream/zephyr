/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/pm/policy.h>
#include <zephyr/sys/util.h>

#include "stay_awake.h"

/*
 * Every suite of the CONFIG_PM build holds this lock for its whole run. The tests
 * read back PMU, PDCON and MEMCON settings that a power state rewrites on entry
 * and exit, so the idle thread must not enter one while a test or the framework
 * waits. The lock covers all states, including those that wake through a reset.
 */
void *stay_awake_setup(void)
{
	pm_policy_state_all_lock_get();

	return NULL;
}

void stay_awake_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	pm_policy_state_all_lock_put();
}
