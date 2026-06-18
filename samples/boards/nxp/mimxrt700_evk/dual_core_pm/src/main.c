/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <fsl_mu.h>

#define RT700_PM_MU_REG kMU_MsgReg0

#define RT700_PM_MSG_CMD_ACTIVE      0x43504d00U
#define RT700_PM_MSG_CMD_SLEEP       0x43504d01U
#define RT700_PM_MSG_CMD_DEEP_SLEEP  0x43504d02U
#define RT700_PM_MSG_CMD_DSR         0x43504d03U
#define RT700_PM_MSG_CMD_GO          0x43504d10U
#define RT700_PM_MSG_ACK_ACTIVE      0x41504d00U
#define RT700_PM_MSG_ACK_SLEEP       0x41504d01U
#define RT700_PM_MSG_ACK_DEEP_SLEEP  0x41504d02U
#define RT700_PM_MSG_ACK_DSR         0x41504d03U
#define RT700_PM_MSG_DONE_ACTIVE     0x44504d00U
#define RT700_PM_MSG_DONE_SLEEP      0x44504d01U
#define RT700_PM_MSG_DONE_DEEP_SLEEP 0x44504d02U
#define RT700_PM_MSG_DONE_DSR        0x44504d03U

enum rt700_pm_mode {
	RT700_PM_MODE_ACTIVE,
	RT700_PM_MODE_SLEEP,
	RT700_PM_MODE_DEEP_SLEEP,
	RT700_PM_MODE_DSR,
};

struct rt700_pm_case {
	const char *name;
	enum rt700_pm_mode cpu0_mode;
	enum rt700_pm_mode cpu1_mode;
};

static const struct rt700_pm_case test_cases[] = {
	{
		.name = "active baseline",
		.cpu0_mode = RT700_PM_MODE_ACTIVE,
		.cpu1_mode = RT700_PM_MODE_ACTIVE,
	},
	{
		.name = "CPU0 sleep, CPU1 active",
		.cpu0_mode = RT700_PM_MODE_SLEEP,
		.cpu1_mode = RT700_PM_MODE_ACTIVE,
	},
	{
		.name = "CPU0 active, CPU1 sleep",
		.cpu0_mode = RT700_PM_MODE_ACTIVE,
		.cpu1_mode = RT700_PM_MODE_SLEEP,
	},
	{
		.name = "CPU0 and CPU1 sleep",
		.cpu0_mode = RT700_PM_MODE_SLEEP,
		.cpu1_mode = RT700_PM_MODE_SLEEP,
	},
	{
		.name = "CPU0 deep sleep, CPU1 active",
		.cpu0_mode = RT700_PM_MODE_DEEP_SLEEP,
		.cpu1_mode = RT700_PM_MODE_ACTIVE,
	},
	{
		.name = "CPU0 active, CPU1 deep sleep",
		.cpu0_mode = RT700_PM_MODE_ACTIVE,
		.cpu1_mode = RT700_PM_MODE_DEEP_SLEEP,
	},
	{
		.name = "CPU0 and CPU1 deep sleep",
		.cpu0_mode = RT700_PM_MODE_DEEP_SLEEP,
		.cpu1_mode = RT700_PM_MODE_DEEP_SLEEP,
	},
	{
		/*
		 * Full deep-sleep retention is a dual-domain (FDSR) mode: it only
		 * engages when both the compute and sense domains request deep
		 * sleep, so CPU1 is parked in deep sleep while CPU0 enters DSR.
		 */
		.name = "CPU0 deep sleep retention, CPU1 deep sleep",
		.cpu0_mode = RT700_PM_MODE_DSR,
		.cpu1_mode = RT700_PM_MODE_DEEP_SLEEP,
	},
};

static atomic_t state_entry_count[PM_STATE_COUNT];
static atomic_t state_exit_count[PM_STATE_COUNT];

static void pm_state_entry(enum pm_state state)
{
	if (state < PM_STATE_COUNT) {
		atomic_inc(&state_entry_count[state]);
	}
}

static void pm_state_exit(enum pm_state state)
{
	if (state < PM_STATE_COUNT) {
		atomic_inc(&state_exit_count[state]);
	}
}

static struct pm_notifier pm_events = {
	.state_entry = pm_state_entry,
	.state_exit = pm_state_exit,
};

static void print_pm_counts(void)
{
	printk("CPU0 PM counts: runtime-idle=%ld/%ld suspend-to-idle=%ld/%ld "
	       "standby=%ld/%ld\n",
	       atomic_get(&state_entry_count[PM_STATE_RUNTIME_IDLE]),
	       atomic_get(&state_exit_count[PM_STATE_RUNTIME_IDLE]),
	       atomic_get(&state_entry_count[PM_STATE_SUSPEND_TO_IDLE]),
	       atomic_get(&state_exit_count[PM_STATE_SUSPEND_TO_IDLE]),
	       atomic_get(&state_entry_count[PM_STATE_STANDBY]),
	       atomic_get(&state_exit_count[PM_STATE_STANDBY]));
}

static uint32_t mode_to_cmd(enum rt700_pm_mode mode)
{
	switch (mode) {
	case RT700_PM_MODE_ACTIVE:
		return RT700_PM_MSG_CMD_ACTIVE;
	case RT700_PM_MODE_SLEEP:
		return RT700_PM_MSG_CMD_SLEEP;
	case RT700_PM_MODE_DEEP_SLEEP:
		return RT700_PM_MSG_CMD_DEEP_SLEEP;
	case RT700_PM_MODE_DSR:
		return RT700_PM_MSG_CMD_DSR;
	default:
		return RT700_PM_MSG_CMD_ACTIVE;
	}
}

static uint32_t mode_to_ack(enum rt700_pm_mode mode)
{
	switch (mode) {
	case RT700_PM_MODE_ACTIVE:
		return RT700_PM_MSG_ACK_ACTIVE;
	case RT700_PM_MODE_SLEEP:
		return RT700_PM_MSG_ACK_SLEEP;
	case RT700_PM_MODE_DEEP_SLEEP:
		return RT700_PM_MSG_ACK_DEEP_SLEEP;
	case RT700_PM_MODE_DSR:
		return RT700_PM_MSG_ACK_DSR;
	default:
		return RT700_PM_MSG_ACK_ACTIVE;
	}
}

static uint32_t mode_to_done(enum rt700_pm_mode mode)
{
	switch (mode) {
	case RT700_PM_MODE_ACTIVE:
		return RT700_PM_MSG_DONE_ACTIVE;
	case RT700_PM_MODE_SLEEP:
		return RT700_PM_MSG_DONE_SLEEP;
	case RT700_PM_MODE_DEEP_SLEEP:
		return RT700_PM_MSG_DONE_DEEP_SLEEP;
	case RT700_PM_MODE_DSR:
		return RT700_PM_MSG_DONE_DSR;
	default:
		return RT700_PM_MSG_DONE_ACTIVE;
	}
}

static const char *mode_to_str(enum rt700_pm_mode mode)
{
	switch (mode) {
	case RT700_PM_MODE_ACTIVE:
		return "active";
	case RT700_PM_MODE_SLEEP:
		return "sleep";
	case RT700_PM_MODE_DEEP_SLEEP:
		return "deep-sleep";
	case RT700_PM_MODE_DSR:
		return "deep-sleep-retention";
	default:
		return "unknown";
	}
}

static uint32_t receive_remote_msg(void)
{
	while ((MU_GetStatusFlags(MU1_MUA) & kMU_Rx0FullFlag) == 0U) {
		k_yield();
	}

	return MU_ReceiveMsgNonBlocking(MU1_MUA, RT700_PM_MU_REG);
}

static void send_remote_msg(uint32_t msg)
{
	(void)MU_SendMsg(MU1_MUA, RT700_PM_MU_REG, msg);
}

static void active_window(void)
{
	for (int i = 0; i < CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS; i++) {
		k_busy_wait(USEC_PER_SEC);
	}
}

static void run_local_window(enum rt700_pm_mode mode)
{
	switch (mode) {
	case RT700_PM_MODE_ACTIVE:
		active_window();
		break;
	case RT700_PM_MODE_SLEEP:
		/*
		 * Suspend-to-idle is locked out at startup, so the idle thread
		 * stays in runtime-idle for the whole window instead of letting
		 * the default policy slide into the deeper state.
		 */
		k_sleep(K_SECONDS(CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS));
		break;
	case RT700_PM_MODE_DEEP_SLEEP:
		/* Temporarily allow suspend-to-idle for this window only. */
		pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
		k_sleep(K_SECONDS(CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS));
		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
		break;
	case RT700_PM_MODE_DSR:
		/* Temporarily allow standby (deep sleep retention) for this window only. */
		pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		k_sleep(K_SECONDS(CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS));
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
		break;
	default:
		break;
	}
}

static void wait_remote_msg(enum rt700_pm_mode mode, uint32_t expected, const char *phase)
{
	uint32_t msg = receive_remote_msg();

	if (msg != expected) {
		printk("CPU0: unexpected CPU1 %s 0x%08x for %s\n", phase, msg, mode_to_str(mode));
	}
}

static void run_test_case(uint32_t index, const struct rt700_pm_case *test_case)
{
	printk("\nCPU0: case %u: %s\n", index, test_case->name);
	printk("CPU0: local=%s remote=%s window=%d seconds\n", mode_to_str(test_case->cpu0_mode),
	       mode_to_str(test_case->cpu1_mode), CONFIG_RT700_DUAL_CORE_PM_SLEEP_SECONDS);

	send_remote_msg(mode_to_cmd(test_case->cpu1_mode));
	wait_remote_msg(test_case->cpu1_mode, mode_to_ack(test_case->cpu1_mode), "ACK");
	send_remote_msg(RT700_PM_MSG_CMD_GO);

	printk("CPU0: entering %s window\n", mode_to_str(test_case->cpu0_mode));
	run_local_window(test_case->cpu0_mode);
	printk("CPU0: local %s window complete\n", mode_to_str(test_case->cpu0_mode));

	wait_remote_msg(test_case->cpu1_mode, mode_to_done(test_case->cpu1_mode), "DONE");
	print_pm_counts();
}

static void print_menu(void)
{
	printk("\nRT700 dual-core PM menu:\n");
	for (uint32_t i = 0; i < ARRAY_SIZE(test_cases); i++) {
		printk("  %u: %s\n", i + 1U, test_cases[i].name);
	}
	printk("  a: run all cases\n");
	printk("Select a case [1-%u, a]: ", (uint32_t)ARRAY_SIZE(test_cases));
}

/*
 * Block until the user types a valid selection. Returns 1..ARRAY_SIZE for a
 * single case, or 0 to run the whole matrix. Other keys (CR, LF, etc.) are
 * ignored so terminals that send line endings do not pick a case by accident.
 */
static uint32_t read_menu_choice(void)
{
	while (true) {
		int c = console_getchar();

		if (c < 0) {
			continue;
		}

		if ((c >= '1') && (c < ('1' + (int)ARRAY_SIZE(test_cases)))) {
			printk("%c\n", c);
			return (uint32_t)(c - '0');
		}

		if ((c == 'a') || (c == 'A')) {
			printk("%c\n", c);
			return 0U;
		}
	}
}

int main(void)
{
	printk("RT700 dual-core PM sample: CPU0 started on %s\n", CONFIG_BOARD_TARGET);
	printk("CPU0: coverage is active, per-core sleep, coordinated sleep, "
	       "per-core deep-sleep, coordinated deep-sleep, deep-sleep-retention\n");

	console_init();
	pm_notifier_register(&pm_events);

	/*
	 * Lock the deeper states by default so sleep windows stay in
	 * runtime-idle. Deep-sleep and DSR windows unlock them explicitly.
	 */
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
	pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);

	while (true) {
		print_menu();

		uint32_t choice = read_menu_choice();

		if (choice == 0U) {
			for (uint32_t i = 0; i < ARRAY_SIZE(test_cases); i++) {
				run_test_case(i + 1U, &test_cases[i]);
			}
		} else {
			run_test_case(choice, &test_cases[choice - 1U]);
		}
	}
}
