/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_CPU_WORKLOAD_INTERNAL_H_
#define ZEPHYR_SUBSYS_CPU_WORKLOAD_INTERNAL_H_

#include <stdint.h>

#include <zephyr/kernel.h>

struct cpu_workload_thread_burst_profile {
	uint32_t burst_avg_cycles;
	uint32_t sample_count;
	uint8_t confidence;
};

int cpu_workload_thread_burst_profile_get(k_tid_t thread,
					  struct cpu_workload_thread_burst_profile *profile);

#endif /* ZEPHYR_SUBSYS_CPU_WORKLOAD_INTERNAL_H_ */
