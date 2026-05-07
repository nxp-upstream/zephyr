/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_CPU_WORKLOAD_H_
#define ZEPHYR_INCLUDE_CPU_WORKLOAD_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU Workload
 * @defgroup subsys_cpu_workload CPU Workload
 * @since 4.4
 * @version 0.1.0
 * @ingroup os_services
 * @{
 */

enum cpu_workload_source {
	/** CPU workload signal used per-thread burst profiles. */
	CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE = BIT(0),

	/** CPU workload signal came from runnable backlog scanning. */
	CPU_WORKLOAD_SOURCE_READY_BACKLOG = BIT(1),

	/** CPU workload signal came from wakeup/arrival attribution. */
	CPU_WORKLOAD_SOURCE_ARRIVAL = BIT(2),

	/** Arrival was caused by a timeout expiry. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_TIMEOUT = BIT(3),

	/** Arrival was caused by a synchronization object becoming ready. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_SYNC = BIT(4),

	/** Arrival was caused by an explicit thread wakeup. */
	CPU_WORKLOAD_SOURCE_ARRIVAL_EXPLICIT = BIT(5),
};

/**
 * @brief Ready backlog workload for one CPU.
 *
 * The backlog reports cycles expected from threads that are runnable at the
 * time of the query. Runnable threads may contribute a configured fallback
 * value until richer per-thread workload profiles are available.
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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_CPU_WORKLOAD_H_ */
