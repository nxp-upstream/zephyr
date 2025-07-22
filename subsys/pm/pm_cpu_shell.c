/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/pm/policy.h>
#include <stdlib.h>

static const struct pm_state_info residency_info[] =
	PM_STATE_INFO_LIST_FROM_DT_CPU(DT_NODELABEL(cpu0));
static size_t residency_info_count = DT_NUM_CPU_POWER_STATES(DT_NODELABEL(cpu0));

static struct k_thread *current_thread;
static bool pm_cpu_forced = true;

bool pm_cpu_shell_forced_idle(void)
{
	return pm_cpu_forced;
}

static void suspend_user_threads_cb(const struct k_thread *thread, void *user_data)
{
	for (thread = _kernel.threads; thread != NULL; thread = thread->next_thread) {
		if (thread == k_current_get())
		{
			/* save current shell thread to suspend in the last sequency */
			current_thread = (struct k_thread *)thread;
		}
		else if(thread != _current_cpu->idle_thread)
		{
			k_thread_suspend((struct k_thread *)thread);
		}
	}
}

static void suspend_user_threads(void) {
       k_thread_foreach(suspend_user_threads_cb, NULL);
       k_thread_suspend(current_thread);
}

static void resume_user_threads_cb(const struct k_thread *thread, void *user_data)
{
	for (thread = _kernel.threads; thread != NULL; thread = thread->next_thread) {
		if(thread != _current_cpu->idle_thread)
                {
                        k_thread_resume((struct k_thread *)thread);
                }
        }
}

void resume_user_threads(void) {
       k_thread_foreach(resume_user_threads_cb, NULL);

       /* restore pm_cpu_forced into true that don't run into soc level low power states in cpu idle
	* only can enter in cpu shell command */
       pm_cpu_forced = true;
}

static const char *pm_state_to_str(enum pm_state state)
{
	switch (state) {
	    case PM_STATE_ACTIVE: return "active";
	    case PM_STATE_RUNTIME_IDLE: return "runtime_idle";
	    case PM_STATE_SUSPEND_TO_IDLE: return "suspend_to_idle";
	    case PM_STATE_STANDBY: return "standby";
	    case PM_STATE_SUSPEND_TO_RAM: return "suspend_to_ram";
	    case PM_STATE_SUSPEND_TO_DISK: return "suspend_to_disk";
	    case PM_STATE_SOFT_OFF: return "soft_off";
	    default: return "UNKNOWN";
    }
}

static int cmd_cpu_lps_info(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Supported Low Power States:");

	for (int i = 0; i < residency_info_count; i++) {
		shell_print(shell,
		"  - State: %s, Substate: %d, Residency: %dus, Latency: %dus, PM Device Disabled: %s",
			pm_state_to_str(residency_info[i].state),
			residency_info[i].substate_id,
			residency_info[i].min_residency_us,
			residency_info[i].exit_latency_us,
			residency_info[i].pm_device_disabled ? "Yes" : "No");
	}

	return 0;
}

static int cmd_cpu_lps(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(shell, "Usage: cpu lps <state>");
		return -EINVAL;
	}

	const char *state_str = argv[1];
	enum pm_state state;
	const struct pm_state_info *info;

	if (strcmp(state_str, "runtime_idle") == 0) {
		state = PM_STATE_RUNTIME_IDLE;
	}
	else if (strcmp(state_str, "suspend_to_idle") == 0) {
		state = PM_STATE_SUSPEND_TO_IDLE;
	}
        else if (strcmp(state_str, "standby") == 0) {
		state = PM_STATE_STANDBY;
	}
        else if (strcmp(state_str, "soft_off") == 0) {
		state = PM_STATE_SOFT_OFF;
	}
	else if (strcmp(state_str, "suspend_to_ram") == 0) {
		state = PM_STATE_SUSPEND_TO_RAM;
	}
        else {
		shell_error(shell, "Unsupported state: %s", state_str);
		return -EINVAL;
	}

	info = pm_state_get(0, state, 0);
	if (!info) {
		shell_error(shell, "Failed to get state info for: %s", state_str);
		return -EINVAL;
	}

	pm_cpu_forced = false;
	pm_state_force(0, info);

	suspend_user_threads();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(cpu_cmds,
    SHELL_CMD(lps_info, NULL, "List supported low power states info", cmd_cpu_lps_info),
    SHELL_CMD(lps, NULL, "Enter low power state using DTS-defined wakeup source", cmd_cpu_lps),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(cpu, &cpu_cmds, "CPU core and power state commands", NULL);

