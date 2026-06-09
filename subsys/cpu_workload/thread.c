/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/util.h>

#include "cpu_workload.h"

#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE 20U
#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP 20U
#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX 100U

/**
 * Map the currently accumulated sample count to a coarse confidence score.
 * No samples report 0; sampled profiles advance in 20-point buckets and
 * saturate at 100.
 */
static uint8_t thread_profile_confidence(uint32_t sample_count)
{
	uint32_t confidence;

	/* If no samples have been collected, the confidence is 0. */
	if (sample_count == 0U) {
		return 0U;
	}

	if (sample_count >= (1U + ((CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX -
				      CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE) /
				      CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP))) {
		return CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX;
	}

	confidence = CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE +
		     ((sample_count - 1U) * CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP);

	return (uint8_t)MIN(confidence, CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX);
}

/**
 * Get the burst profile of a thread, including the average activation cycles, sample count,
 * and confidence.
 */
int cpu_workload_thread_burst_profile_get(k_tid_t thread,
					  struct cpu_workload_thread_burst_profile *profile)
{
	k_thread_activation_stats_t activation_stats;
	uint32_t burst_cycles = 0U;
	int ret;

	CHECKIF((thread == NULL) || (profile == NULL)) {
		return -EINVAL;
	}

	ret = k_thread_activation_stats_get(thread, &activation_stats, false);
	if (ret != 0) {
		return ret;
	}

	if (activation_stats.completed_count != 0U) {
		burst_cycles = (uint32_t)MIN(activation_stats.completed_cycles /
						 activation_stats.completed_count,
						 (uint64_t)UINT32_MAX);
	}

	profile->activation_based = activation_stats.completed_count != 0U;
	profile->burst_avg_cycles = burst_cycles;
	profile->sample_count = activation_stats.completed_count;
	profile->event_count = activation_stats.completed_events;
	profile->confidence = thread_profile_confidence(activation_stats.completed_count);

	return 0;
}
