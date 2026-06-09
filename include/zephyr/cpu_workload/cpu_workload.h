/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_CPU_WORKLOAD_H_
#define ZEPHYR_INCLUDE_CPU_WORKLOAD_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU Workload
 * @defgroup subsys_cpu_workload CPU Workload
 * @since 4.5
 * @version 0.1.0
 * @ingroup os_services
 * @{
 */

enum cpu_workload_source {
	/** Recent runtime history contributed to this workload. */
	CPU_WORKLOAD_SOURCE_RUNTIME_HISTORY = BIT(0),

	/** Per-thread burst profiles contributed to this workload. */
	CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE = BIT(1),

	/** Complete thread activation profiles contributed to this workload. */
	CPU_WORKLOAD_SOURCE_THREAD_ACTIVATION_PROFILE = BIT(7),

	/** Runnable backlog scanning contributed to this workload. */
	CPU_WORKLOAD_SOURCE_READY_BACKLOG = BIT(2),

	/** Wakeup/arrival attribution contributed to this workload. */
	CPU_WORKLOAD_SOURCE_ARRIVAL = BIT(3),

	/** Arrival was caused by a timeout expiry. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_TIMEOUT = BIT(4),

	/** Arrival was caused by a synchronization object becoming ready. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_SYNC = BIT(5),

	/** Arrival was caused by an explicit thread wakeup. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_EXPLICIT = BIT(6),
};

enum cpu_workload_contributor_flag {
	/** Contributor was runnable at the time of the diagnostic scan. */
	CPU_WORKLOAD_CONTRIBUTOR_RUNNABLE = BIT(0),

	/** Contributor had a usable per-thread burst profile. */
	CPU_WORKLOAD_CONTRIBUTOR_PROFILED = BIT(1),

	/** Contributor contributed to ready-backlog cycles. */
	CPU_WORKLOAD_CONTRIBUTOR_READY_BACKLOG = BIT(2),

	/** Contributor had attributed arrivals since the previous query. */
	CPU_WORKLOAD_CONTRIBUTOR_ARRIVAL = BIT(3),

	/** Contributor is the thread executing the diagnostic query. */
	CPU_WORKLOAD_CONTRIBUTOR_CURRENT_THREAD = BIT(4),

	/** Contributor used a complete activation profile. */
	CPU_WORKLOAD_CONTRIBUTOR_ACTIVATION_PROFILE = BIT(5),
};

/**
 * @brief Per-thread diagnostic contributor for a CPU workload estimate.
 *
 * This structure is intended for debugging and evaluation samples. It exposes
 * which threads are visible to ready-backlog and arrival attribution before a
 * unified estimate consumes and resets the arrival window.
 */
struct cpu_workload_contributor {
	/** Numeric thread identity for log correlation. */
	uintptr_t thread_id;

	/** Thread name when CONFIG_THREAD_NAME is enabled, otherwise NULL. */
	const char *thread_name;

	/** Contributor flags from enum cpu_workload_contributor_flag. */
	uint32_t flags;

	/** Arrival source mask using CPU_WORKLOAD_SOURCE_ARRIVAL_* bits. */
	uint32_t arrival_source_mask;

	/** Per-thread burst-profile average cycles. */
	uint32_t burst_avg_cycles;

	/** Number of samples used by the thread burst profile. */
	uint32_t sample_count;

	/** Number of activation events represented by the thread burst profile. */
	uint32_t event_count;

	/** Cycles this thread contributes to ready backlog. */
	uint64_t backlog_cycles;

	/** Cycles this thread contributes through arrival attribution. */
	uint64_t arrival_cycles;

	/** Cycles this thread contributes after ready/arrival merge. */
	uint64_t merged_cycles;

	/** Number of arrivals attributed to this thread. */
	uint16_t arrival_count;

	/** Confidence in the per-thread burst profile. */
	uint8_t confidence;
};

/**
 * @brief Runtime-history workload for one CPU.
 *
 * The history reports CPU runtime-stat deltas observed since the previous
 * history query. The first query establishes the baseline and returns
 * -EAGAIN without reporting a sample.
 */
struct cpu_workload_history {
	/** Non-idle CPU cycles observed since the previous history query. */
	uint64_t non_idle_cycles;

	/** Total CPU runtime-stat cycles elapsed since the previous history query. */
	uint64_t window_cycles;

	/** Runtime-history window duration converted to microseconds. */
	uint32_t window_us;

	/** Non-idle load percentage within the runtime-history window. */
	uint8_t load;

	/** Confidence in the runtime-history sample, from 0 to 100. */
	uint8_t confidence;
};

/**
 * @brief Ready backlog workload for one CPU.
 *
 * The backlog reports cycles expected from profiled threads that are runnable
 * at the time of the query.
 */
struct cpu_workload_ready_backlog {
	/** Estimated cycles currently queued as runnable work. */
	uint64_t ready_backlog_cycles;

	/** Bitmask describing which sources contributed to the sample. */
	uint32_t source_mask;

	/** Number of runnable threads considered by the scan. */
	uint16_t runnable_threads;

	/** Number of runnable threads with usable burst profiles. */
	uint16_t profiled_threads;

	/** Confidence in the sample, from 0 to 100. */
	uint8_t confidence;
};

/**
 * @brief CPU wakeup-arrival workload for one CPU.
 *
 * The arrival reports expected cycles from threads that were woken since the
 * previous query. Each attributed arrival contributes the target thread's
 * per-thread burst profile when one is available.
 */
struct cpu_workload_arrival {
	/** Estimated cycles expected from recently attributed arrivals. */
	uint64_t expected_arrival_cycles;

	/** Bitmask describing which sources contributed to the sample. */
	uint32_t source_mask;

	/** Number of arrivals observed since the previous query. */
	uint16_t arrivals;

	/** Number of arrivals with usable burst profiles. */
	uint16_t profiled_arrivals;

	/** Confidence in the sample, from 0 to 100. */
	uint8_t confidence;
};

/**
 * @brief Get CPU runtime-history workload.
 *
 * @param cpu_id The ID of the CPU for which to get the runtime history.
 * @param history Pointer to the output runtime history.
 *
 * @retval 0 If the runtime history was written.
 * @retval -EAGAIN If this query only established the history baseline.
 * @retval -EINVAL If @p cpu_id or @p history is invalid.
 * @retval -ENOTSUP If runtime-history sampling is not enabled.
 */
int cpu_workload_history_get(int cpu_id, struct cpu_workload_history *history);

/**
 * @brief Unified CPU workload estimate for one CPU.
 *
 * The estimate combines recent runtime history with forward-looking workload
 * workloads. Runtime history is used as a conservative floor for recurring work,
 * while ready backlog and arrival workloads describe work that is already queued
 * or recently arrived.
 */
struct cpu_workload_estimate {
	/** Estimated cycles expected for the next decision window. */
	uint64_t estimated_cycles;

	/** Estimated cycles from currently runnable work. */
	uint64_t ready_backlog_cycles;

	/** Estimated cycles from recently attributed arrivals. */
	uint64_t expected_arrival_cycles;

	/** Forward-looking cycles after merging ready and arrival work per thread. */
	uint64_t forward_cycles;

	/** Recent non-idle runtime-history cycles. */
	uint64_t history_cycles;

	/** Runtime-history window duration in microseconds. */
	uint32_t history_window_us;

	/** Number of runnable threads considered by the ready backlog scan. */
	uint16_t runnable_threads;

	/** Number of runnable threads with usable burst profiles. */
	uint16_t profiled_runnable_threads;

	/** Number of attributed arrivals since the previous query. */
	uint16_t arrivals;

	/** Number of attributed arrivals with usable burst profiles. */
	uint16_t profiled_arrivals;

	/** Bitmask describing which sources contributed to the estimate. */
	uint32_t source_mask;

	/** Recent runtime-history load percentage. */
	uint8_t history_load;

	/** Confidence in the estimate, from 0 to 100. */
	uint8_t confidence;
};

/**
 * @brief Get CPU ready-backlog workload.
 *
 * @param cpu_id The ID of the CPU for which to get the ready backlog.
 * @param backlog Pointer to the output ready backlog.
 *
 * @retval 0 If the ready backlog was written.
 * @retval -EINVAL If @p cpu_id or @p backlog is invalid.
 * @retval -ENOTSUP If ready-backlog sampling is not enabled.
 */
int cpu_workload_ready_backlog_get(int cpu_id, struct cpu_workload_ready_backlog *backlog);

/**
 * @brief Get CPU wakeup-arrival workload.
 *
 * @param cpu_id The ID of the CPU for which to get the arrival workload.
 * @param arrival Pointer to the output arrival workload.
 *
 * @retval 0 If the arrival workload was written.
 * @retval -EINVAL If @p cpu_id or @p arrival is invalid.
 * @retval -ENOTSUP If arrival attribution is not enabled.
 */
int cpu_workload_arrival_get(int cpu_id, struct cpu_workload_arrival *arrival);

/**
 * @brief Get a unified CPU workload estimate.
 *
 * @param cpu_id The ID of the CPU for which to get the estimate.
 * @param estimate Pointer to the output estimate.
 *
 * @retval 0 If an estimate was written.
 * @retval -EINVAL If @p cpu_id or @p estimate is invalid.
 * @retval -ENOTSUP If unified workload estimation is not enabled.
 */
int cpu_workload_estimate_get(int cpu_id, struct cpu_workload_estimate *estimate);

/**
 * @brief Get diagnostic per-thread contributors for a CPU workload estimate.
 *
 * The caller supplies @p contributors with @p contributor_count set to the
 * array capacity. On return, @p contributor_count contains the number of
 * entries written. Arrival statistics are read without resetting them.
 *
 * @param cpu_id The ID of the CPU for which to get diagnostic contributors.
 * @param contributors Pointer to an output contributor array.
 * @param contributor_count In/out contributor array capacity and written count.
 *
 * @retval 0 If all contributors were written.
 * @retval -EINVAL If parameters are invalid.
 * @retval -ENOMEM If the output array was too small and the result was truncated.
 * @retval -ENOTSUP If contributor diagnostics are not enabled.
 */
int cpu_workload_contributors_get(int cpu_id, struct cpu_workload_contributor *contributors,
					  size_t *contributor_count);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_CPU_WORKLOAD_H_ */
