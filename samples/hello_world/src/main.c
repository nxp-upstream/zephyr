/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>

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

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	GPIO5->PDDR |= 1 << 3U;
	GPIO5->PCOR = 1 << 3U;

	/* Create a thread to monitor scheduling */
	k_thread_create(&thread_data, thread_stack, K_THREAD_STACK_SIZEOF(thread_stack),
			thread_monitor, NULL, NULL, NULL, 7, 0, K_NO_WAIT);

	return 0;
}
