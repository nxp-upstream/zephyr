/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_CPU_WORKLOAD_H_
#define ZEPHYR_SUBSYS_CPU_WORKLOAD_H_

#include <stdint.h>

#include <zephyr/kernel.h>

/**
 * @brief Per-thread burst workload profile.
 *
 * The profile describes a thread's typical complete activation cost. It is
 * consumed by cpu_workload code that needs a per-thread cost estimate, such as
 * ready-backlog and arrival attribution.
 */
struct cpu_workload_thread_burst_profile {
	/** Average CPU cycles consumed per completed activation. */
	uint32_t burst_avg_cycles;

	/** Number of completed activations observed for the thread. */
	uint32_t sample_count;

	/** Number of scheduler arrivals represented by activation samples. */
	uint32_t event_count;

	/**
	 * Confidence in @ref burst_avg_cycles.
	 *
	 * No samples report 0; sampled profiles report 20 to 100.
	 */
	uint8_t confidence;

	/** True if @ref burst_avg_cycles was learned from complete activations. */
	bool activation_based;
};

/**
 * @brief Get or update the burst profile for a thread.
 *
 * The query reads the thread's cumulative activation statistics and writes the
 * current burst profile to @p profile.
 *
 * @param thread Thread for which to get the burst profile.
 * @param profile Pointer to the output burst profile.
 *
 * @retval 0 If the burst profile was written.
 * @retval -EINVAL If @p thread or @p profile is invalid.
 * @retval -ENOTSUP If activation statistics are not enabled.
 */
int cpu_workload_thread_burst_profile_get(k_tid_t thread,
					  struct cpu_workload_thread_burst_profile *profile);

#endif /* ZEPHYR_SUBSYS_CPU_WORKLOAD_H_ */
