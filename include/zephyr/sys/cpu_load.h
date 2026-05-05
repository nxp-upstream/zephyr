/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_CPU_LOAD_H_
#define ZEPHYR_SUBSYS_CPU_LOAD_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU Load Monitoring
 * @defgroup subsys_cpu_load CPU Load
 * @since 4.3
 * @version 0.1.0
 * @ingroup os_services
 * @{
 */

/** CPU load window came from runtime-history counters. */
#define CPU_LOAD_WINDOW_SOURCE_RUNTIME_HISTORY BIT(0)

/**
 * @brief CPU load data for one recent accounting window.
 *
 * The window reports non-idle CPU cycles observed since the previous
 * successful window read for @p cpu_id. A successful read advances the
 * per-CPU accounting baseline used by both cpu_load_window_get() and
 * cpu_load_metric_get(). Users may treat @p non_idle_cycles as a
 * first-order workload estimate for the next comparable window, with
 * @p confidence indicating how much history has been collected.
 */
struct cpu_load_window {
	/** Non-idle cycles in the accounting window. */
	uint64_t non_idle_cycles;

	/** Accounting window duration in microseconds. */
	uint32_t window_us;

	/** Bitmask describing which sources contributed to the window. */
	uint32_t source_mask;

	/** Percentage of the window spent executing non-idle work. */
	uint8_t load;

	/** Confidence in the window, from 0 to 100. */
	uint8_t confidence;
};

/**
 * @brief Get a CPU load accounting window.
 *
 * A successful call writes the most recent complete accounting window and
 * advances the per-CPU baseline for the next call. The first call for a CPU
 * seeds that baseline and returns -EAGAIN.
 *
 * @param cpu_id The ID of the CPU for which to get the window.
 * @param window Pointer to the output window.
 *
 * @retval 0 If a window was written.
 * @retval -EINVAL If @p cpu_id or @p window is invalid.
 * @retval -EAGAIN If the metric has not accumulated a complete window yet.
 * @retval -errno Other errors returned by runtime statistics collection.
 */
int cpu_load_window_get(int cpu_id, struct cpu_load_window *window);

/**
 * @brief Get the CPU load as a percentage.
 *
 * Return the percent that the CPU has spent in the active (non-idle) state between calls to this
 * function. This API shares its accounting baseline with cpu_load_window_get().
 *
 * @param cpu_id The ID of the CPU for which to get the load.
 *
 * @return CPU load in percent (0-100) in case of success
 * @retval -errno code in case of failure.
 *
 */
int cpu_load_metric_get(int cpu_id);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_CPU_LOAD_H_ */
