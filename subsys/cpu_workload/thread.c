/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include "cpu_workload_internal.h"

#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE 20U
#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP 10U
#define CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX 80U

struct cpu_workload_thread_profile_slot {
	k_tid_t thread;
	uint64_t observed_total_cycles;
	uint32_t observed_windows;
	uint32_t sample_count;
	uint32_t burst_last_cycles;
	uint32_t burst_avg_cycles;
};

static struct k_spinlock thread_profile_lock;
static struct cpu_workload_thread_profile_slot
	thread_profile_slots[CONFIG_CPU_WORKLOAD_THREAD_PROFILE_CACHE_SIZE];

static struct cpu_workload_thread_profile_slot *thread_profile_slot_get(k_tid_t thread)
{
	struct cpu_workload_thread_profile_slot *free_slot = NULL;

	for (size_t i = 0U; i < ARRAY_SIZE(thread_profile_slots); i++) {
		if (thread_profile_slots[i].thread == thread) {
			return &thread_profile_slots[i];
		}

		if ((free_slot == NULL) && (thread_profile_slots[i].thread == NULL)) {
			free_slot = &thread_profile_slots[i];
		}
	}

	if (free_slot != NULL) {
		free_slot->thread = thread;
	}

	return free_slot;
}

/*
 * Calculate completed scheduling windows since the last sample.
 */
static uint32_t thread_profile_completed_delta(const k_thread_runtime_stats_t *stats,
					       struct cpu_workload_thread_profile_slot *slot)
{
	uint32_t window_delta;

	/* If the new window_count/total_cycles is less than the old ledger, then the
	 * old portrait can no longer be used and must be cleared and re-learned.
	 */
	if ((stats->window_count < slot->observed_windows) ||
	    (stats->total_cycles < slot->observed_total_cycles)) {
		slot->observed_windows = 0U;
		slot->observed_total_cycles = 0U;
		slot->sample_count = 0U;
		slot->burst_last_cycles = 0U;
		slot->burst_avg_cycles = 0U;
	}

	/* Calculate the average number of cycles this thread ran per window during
	 * this newly added observation period — (single burst length).
	 */
	window_delta = stats->window_count - slot->observed_windows;

	if (stats->total_cycles == slot->observed_total_cycles) {
		return 0U;
	}

	if (slot->sample_count == 0U) {
		if (stats->window_count <= 1U) {
			return 1U;
		}

		return stats->window_count - 1U;
	}

	return MAX(window_delta, 1U);
}

/**
 * Use EWMA (Exponential Weighted Moving Average) to merge the newly observed burst_cycles
 * into the long-term maintained burst_avg_cycles of the slot, so that the thread profile
 * can both follow changes in thread behavior and avoid being skewed by single-time fluctuations.
 */
static void thread_profile_update_avg(struct cpu_workload_thread_profile_slot *slot,
				      uint32_t burst_cycles)
{
	uint32_t avg = slot->burst_avg_cycles;

	/**
	 * 1. When avg == 0 (first sample), directly initialize with the sample value to avoid
	 *    starting from 0 and slowly climbing up, which would cause it to remain low for an
	 *    extended period.
	 *
	 * 2. burst_cycles >= avg (sample is larger than average), avg += (sample - avg) >> shift:
	 *    Uses unsigned subtraction to first calculate the positive difference, then
	 *    right-shifts it. This avoids implementation-defined behavior of signed
	 *    arithmetic / negative number right-shifting.
	 *
	 * 3. burst_cycles < avg (sample is smaller than average), avg -= (avg - sample) >> shift:
	 *    Similar to the above, but in reverse to decrease the average when the new sample is
	 *    lower.
	 */
	if (avg == 0U) {
		slot->burst_avg_cycles = burst_cycles;
	} else if (burst_cycles >= avg) {
		slot->burst_avg_cycles = avg +
			((burst_cycles - avg) >> CONFIG_CPU_WORKLOAD_THREAD_PROFILE_EWMA_SHIFT);
	} else {
		slot->burst_avg_cycles = avg -
			((avg - burst_cycles) >> CONFIG_CPU_WORKLOAD_THREAD_PROFILE_EWMA_SHIFT);
	}
}

static uint32_t thread_profile_predict_cycles(const struct cpu_workload_thread_profile_slot *slot)
{
	if (slot->sample_count == 0U) {
		return 0U;
	}

	return MAX(slot->burst_last_cycles, slot->burst_avg_cycles);
}

/**
 * Map the currently accumulated sample count (sample_count) to a confidence score ranging
 * from 0 to 80, indicating how reliable the current burst_avg_cycles profile is. It is
 * metadata exposed externally together with burst_avg_cycles — the value itself + how much
 * reference value this value has.
 */
static uint8_t thread_profile_confidence(uint32_t sample_count)
{
	uint32_t confidence;

	/* If no samples have been collected, the confidence is 0. */
	if (sample_count == 0U) {
		return 0U;
	}

	/* For every additional CONFIG_CPU_WORKLOAD_THREAD_PROFILE_EWMA_SHIFT samples collected,
	 * the confidence increases by CONFIG_CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP, until
	 * it reaches the maximum.
	 */
	if (sample_count >= ((CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX -
			      CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE) /
			      CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP)) {
		return CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX;
	}

	/* Reached sufficient samples threshold, capped */
	confidence = CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_BASE +
		     (sample_count * CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_STEP);

	return (uint8_t)MIN(confidence, CPU_WORKLOAD_THREAD_PROFILE_CONFIDENCE_MAX);
}

/**
 * Get the burst profile of a thread, including the average burst cycles, sample count,
 * and confidence.
 *
 * burst_avg_cycles is smoothed by EWMA → reflects the estimated value;
 * confidence grows linearly with sample count → reflects how many observations this estimate
 * is based on.
 *
 * burst_avg_cycles and confidence are orthogonal, allowing users to combine them according
 * to policy (e.g., fall back to default behavior when confidence is low).
 *
 * Uses the absolute cumulative window_count, not delta: this means that as long as a thread
 * has been scheduled enough times in its history, confidence will saturate; this complements
 * EWMA's semantics of 'continuously refreshing with new samples'.
 */
int cpu_workload_thread_burst_profile_get(k_tid_t thread,
					  struct cpu_workload_thread_burst_profile *profile)
{
	struct cpu_workload_thread_profile_slot *slot;
	k_spinlock_key_t key;
	uint32_t completed_delta;
	uint32_t burst_cycles;
	uint64_t cycle_delta;
	k_thread_runtime_stats_t stats;
	int ret;

	if ((thread == NULL) || (profile == NULL)) {
		return -EINVAL;
	}

	ret = k_thread_runtime_stats_get(thread, &stats);
	if (ret != 0) {
		return ret;
	}

	key = k_spin_lock(&thread_profile_lock);
	slot = thread_profile_slot_get(thread);
	if (slot == NULL) {
		k_spin_unlock(&thread_profile_lock, key);
		return -ENOMEM;
	}

	completed_delta = thread_profile_completed_delta(&stats, slot);
	cycle_delta = stats.total_cycles - slot->observed_total_cycles;
	if ((completed_delta != 0U) && (cycle_delta != 0U)) {
		burst_cycles = (uint32_t)MIN(cycle_delta / completed_delta, (uint64_t)UINT32_MAX);
		slot->burst_last_cycles = (stats.current_cycles == 0U) ? burst_cycles :
			(uint32_t)MIN(stats.current_cycles, (uint64_t)UINT32_MAX);
		thread_profile_update_avg(slot, burst_cycles);
		if ((UINT32_MAX - slot->sample_count) < completed_delta) {
			slot->sample_count = UINT32_MAX;
		} else {
			slot->sample_count += completed_delta;
		}
	}

	slot->observed_windows = stats.window_count;
	slot->observed_total_cycles = stats.total_cycles;

	profile->burst_last_cycles = slot->burst_last_cycles;
	profile->burst_avg_cycles = slot->burst_avg_cycles;
	profile->predicted_cycles = thread_profile_predict_cycles(slot);
	profile->sample_count = slot->sample_count;
	profile->confidence = thread_profile_confidence(slot->sample_count);

	k_spin_unlock(&thread_profile_lock, key);

	return 0;
}
