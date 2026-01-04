/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(cpu_freq_sample, LOG_LEVEL_INF);

#define MS_TO_US(x)      ((x) * 1000)
#define PERCENT_SLEEP_MS (CONFIG_CPU_FREQ_INTERVAL_MS / 100)

typedef enum {
	CPU_SCENARIO_0,
	CPU_SCENARIO_1,
	CPU_SCENARIO_2,
	CPU_SCENARIO_3,
	CPU_SCENARIO_4,
	CPU_SCENARIO_COUNT
} cpu_scenario_t;

static int curr_work_time_ms;
static cpu_scenario_t cpu_scenario = CPU_SCENARIO_0;

static void update_sleep_time(struct k_timer *timer_id)
{
	cpu_scenario = (cpu_scenario + 1) % CPU_SCENARIO_COUNT;

	switch (cpu_scenario) {
	case CPU_SCENARIO_0:
		/* CPU is always in sleep */
		curr_work_time_ms = 0;
		break;
	case CPU_SCENARIO_1:
		/* CPU doing some intermittent processing */
		curr_work_time_ms = PERCENT_SLEEP_MS * 10;
		break;
	case CPU_SCENARIO_2:
		/* CPU doing some more intense processing */
		curr_work_time_ms = PERCENT_SLEEP_MS * 25;
		break;
	case CPU_SCENARIO_3:
		/* CPU doing some more intense processing */
		curr_work_time_ms = PERCENT_SLEEP_MS * 50;
		break;
	case CPU_SCENARIO_4:
		/* CPU doing lots of calculations */
		curr_work_time_ms = PERCENT_SLEEP_MS * 100;
		break;
	default:
		LOG_ERR("Unknown CPU scenario: %d", cpu_scenario);
	}
}

K_TIMER_DEFINE(timer, update_sleep_time, NULL);
#define THREAD_STACK_SIZE	1024
K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread_data;

static void thread_monitor(void *arg1, void *arg2, void *arg3) {
	bool state = true;

	while (1) {
		if (state) {
			GPIO5->PSOR = 1 << 3U; /* set pin high */
		} else {
			GPIO5->PCOR = 1 << 3U; /* set pin low */
		}
		state = !state;
		k_sleep(K_MSEC(1000)); // Expected to run every 1000ms
	}
}

int main(void) {
	LOG_INF("Starting CPU Freq Subsystem Sample!");

	curr_work_time_ms = 0;
	GPIO5->PDDR |= 1 << 3U;
	GPIO5->PCOR = 1 << 3U;

	/* Set timer to change cpu scenario periodically */
	k_timer_start(&timer, K_MSEC(CONFIG_CPU_FREQ_INTERVAL_MS),
		      K_MSEC(CONFIG_CPU_FREQ_INTERVAL_MS));

	/* Create a thread to monitor scheduling */
	k_thread_create(&thread_data, thread_stack, K_THREAD_STACK_SIZEOF(thread_stack),
			thread_monitor, NULL, NULL, NULL, 7, 0, K_NO_WAIT);

	while (1) {
		k_busy_wait(MS_TO_US(curr_work_time_ms));
		k_msleep(CONFIG_CPU_FREQ_INTERVAL_MS - curr_work_time_ms);
	}
}
