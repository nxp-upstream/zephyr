/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/cpu_workload/cpu_workload.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/util.h>

#include "cpu_workload.h"

LOG_MODULE_REGISTER(cpu_workload_estimate, CONFIG_CPU_WORKLOAD_LOG_LEVEL);

#define CPU_WORKLOAD_BACKLOG_EMPTY_CONFIDENCE 100U

struct estimate_forward_context {
	struct cpu_workload_estimate *estimate;
	uint8_t backlog_confidence;
	uint8_t arrival_confidence;
};

/* Merge the confidence of multiple workloads, where the overall confidence
 * equals to the minimum value of all participating workloads' confidence.
 *
 * Conservative strategy: if any one source is not very reliable, the overall
 * estimation should not be given too high a confidence.
 */
static void confidence_merge(uint8_t *confidence, uint8_t next_confidence)
{
	if (*confidence == UINT8_MAX) {
		*confidence = next_confidence;
		return;
	}

	*confidence = MIN(*confidence, next_confidence);
}

static uint32_t estimate_arrival_source_mask(uint32_t source_mask)
{
	uint32_t workload_mask = 0U;

	if ((source_mask & K_THREAD_ARRIVAL_SOURCE_TIMEOUT) != 0U) {
		workload_mask |= CPU_WORKLOAD_SOURCE_ARRIVAL_TIMEOUT;
	}

	if ((source_mask & K_THREAD_ARRIVAL_SOURCE_SYNC) != 0U) {
		workload_mask |= CPU_WORKLOAD_SOURCE_ARRIVAL_SYNC;
	}

	if ((source_mask & K_THREAD_ARRIVAL_SOURCE_EXPLICIT) != 0U) {
		workload_mask |= CPU_WORKLOAD_SOURCE_ARRIVAL_EXPLICIT;
	}

	return workload_mask;
}

static void estimate_forward_cb(const struct k_thread *thread, void *user_data)
{
	struct estimate_forward_context *ctx = user_data;
	struct cpu_workload_thread_burst_profile profile;
	struct cpu_workload_estimate *estimate = ctx->estimate;
	k_thread_arrival_stats_t stats;
	uint64_t backlog_cycles = 0U;
	uint64_t arrival_cycles = 0U;
	bool has_profile = false;
	bool runnable;
	int ret;

	runnable = (thread->base.thread_state & _THREAD_QUEUED) != 0;
	if (runnable) {
		estimate->runnable_threads++;
	}

	ret = cpu_workload_thread_burst_profile_get((k_tid_t)thread, &profile);
	if ((ret == 0) && (profile.sample_count > 0U)) {
		has_profile = true;
	}

	ret = k_thread_arrival_stats_get((k_tid_t)thread, &stats, true);
	if ((ret == 0) && (stats.count > 0U)) {
		estimate->source_mask |= CPU_WORKLOAD_SOURCE_ARRIVAL |
			estimate_arrival_source_mask(stats.source_mask);
		estimate->arrivals += stats.count;

		if (has_profile) {
			arrival_cycles = (uint64_t)profile.burst_avg_cycles * stats.count;
			estimate->expected_arrival_cycles += arrival_cycles;
			estimate->profiled_arrivals += stats.count;
			estimate->source_mask |= CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE;
			if (profile.activation_based) {
				estimate->source_mask |= CPU_WORKLOAD_SOURCE_THREAD_ACTIVATION_PROFILE;
			}
			confidence_merge(&ctx->arrival_confidence, profile.confidence);
		} else {
			ctx->arrival_confidence = 0U;
		}
	}

	if (!runnable) {
		estimate->forward_cycles += arrival_cycles;
		return;
	}

	if (has_profile &&
	    (profile.confidence >= CONFIG_CPU_WORKLOAD_READY_BACKLOG_MIN_CONFIDENCE)) {
		backlog_cycles = profile.burst_avg_cycles;
		estimate->ready_backlog_cycles += backlog_cycles;
		estimate->profiled_runnable_threads++;
		estimate->source_mask |= CPU_WORKLOAD_SOURCE_THREAD_BURST_PROFILE;
		if (profile.activation_based) {
			estimate->source_mask |= CPU_WORKLOAD_SOURCE_THREAD_ACTIVATION_PROFILE;
		}
		confidence_merge(&ctx->backlog_confidence, profile.confidence);
	} else {
		ctx->backlog_confidence = 0U;
	}

	estimate->forward_cycles += MAX(backlog_cycles, arrival_cycles);
}

static void estimate_forward_get(int cpu_id, struct cpu_workload_estimate *estimate,
				 uint8_t *confidence)
{
	struct estimate_forward_context ctx = {
		.estimate = estimate,
		.backlog_confidence = UINT8_MAX,
		.arrival_confidence = UINT8_MAX,
	};

	estimate->source_mask |= CPU_WORKLOAD_SOURCE_READY_BACKLOG;
	k_thread_foreach_filter_by_cpu((unsigned int)cpu_id, estimate_forward_cb, &ctx);

	if (estimate->runnable_threads == 0U) {
		confidence_merge(confidence, CPU_WORKLOAD_BACKLOG_EMPTY_CONFIDENCE);
	} else if (ctx.backlog_confidence != UINT8_MAX) {
		confidence_merge(confidence, ctx.backlog_confidence);
	}

	if (estimate->arrivals > 0U) {
		if (ctx.arrival_confidence != UINT8_MAX) {
			confidence_merge(confidence, ctx.arrival_confidence);
		} else {
			confidence_merge(confidence, 0U);
		}
	}
}

int cpu_workload_estimate_get(int cpu_id, struct cpu_workload_estimate *estimate)
{
	struct cpu_workload_history history;
	uint8_t confidence = UINT8_MAX;
	int ret;

	CHECKIF((estimate == NULL) || (cpu_id < 0) || (cpu_id >= arch_num_cpus())) {
		return -EINVAL;
	}

	estimate->estimated_cycles = 0U;
	estimate->ready_backlog_cycles = 0U;
	estimate->expected_arrival_cycles = 0U;
	estimate->forward_cycles = 0U;
	estimate->history_cycles = 0U;
	estimate->history_window_us = 0U;
	estimate->runnable_threads = 0U;
	estimate->profiled_runnable_threads = 0U;
	estimate->arrivals = 0U;
	estimate->profiled_arrivals = 0U;
	estimate->source_mask = 0U;
	estimate->history_load = 0U;
	estimate->confidence = 0U;

	estimate_forward_get(cpu_id, estimate, &confidence);

	/* Get how many non-idle cycles this CPU actually ran in the most recent runtime window.
	 *
	 * Runtime history is optional here. The system might have just started up,
	 * or the window might not be ready yet, so not having history at this point is not
	 * a hard error.
	 */
	ret = cpu_workload_history_get(cpu_id, &history);
	if (ret == 0) {
		estimate->history_cycles = history.non_idle_cycles;
		estimate->history_window_us = history.window_us;
		estimate->history_load = history.load;
		estimate->source_mask |= CPU_WORKLOAD_SOURCE_RUNTIME_HISTORY;
		confidence_merge(&confidence, history.confidence);
	} else if (ret != -EAGAIN) {
		LOG_DBG("CPU%d runtime-history window unavailable: %d", cpu_id, ret);
	}

	/* estimated_cycles takes MAX(forward_cycles, history_cycles), where
	 * forward_cycles represents the currently visible future workload,
	 * while runtime history serves as a conservative lower bound for sustained load.
	 *
	 * Ready backlog and arrival are merged per thread before summing so a just-woken
	 * runnable thread does not contribute the same burst to both components.
	 */
	estimate->estimated_cycles = MAX(estimate->forward_cycles, estimate->history_cycles);

	if (confidence != UINT8_MAX) {
		estimate->confidence = confidence;
	}

	LOG_DBG("CPU%d estimate: cycles=%llu forward=%llu history=%llu source=0x%x confidence=%u",
		cpu_id, (unsigned long long)estimate->estimated_cycles,
		(unsigned long long)estimate->forward_cycles,
		(unsigned long long)estimate->history_cycles, estimate->source_mask,
		estimate->confidence);

	return 0;
}
