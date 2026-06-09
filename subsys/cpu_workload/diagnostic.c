/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/cpu_workload/cpu_workload.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/util.h>

#include "cpu_workload.h"

#if defined(CONFIG_CPU_WORKLOAD_DIAGNOSTIC)

struct contributor_context {
	struct cpu_workload_contributor *contributors;
	size_t capacity;
	size_t count;
	bool truncated;
};

static uint32_t diagnostic_arrival_source_mask(uint32_t source_mask)
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

static void contributor_append(struct contributor_context *ctx,
			       const struct cpu_workload_contributor *contributor)
{
	if (ctx->count >= ctx->capacity) {
		ctx->truncated = true;
		return;
	}

	ctx->contributors[ctx->count] = *contributor;
	ctx->count++;
}

static void contributor_cb(const struct k_thread *thread, void *user_data)
{
	struct cpu_workload_thread_burst_profile profile;
	struct contributor_context *ctx = user_data;
	struct cpu_workload_contributor contributor;
	k_thread_arrival_stats_t stats;
	bool has_profile = false;
	bool has_arrival = false;
	bool runnable;
	int ret;

	memset(&contributor, 0, sizeof(contributor));
	memset(&stats, 0, sizeof(stats));

	runnable = (thread->base.thread_state & _THREAD_QUEUED) != 0;
	if (runnable) {
		contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_RUNNABLE;
	}

	if (thread == k_current_get()) {
		contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_CURRENT_THREAD;
	}

	ret = cpu_workload_thread_burst_profile_get((k_tid_t)thread, &profile);
	if ((ret == 0) && (profile.sample_count > 0U)) {
		has_profile = true;
		contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_PROFILED;
		if (profile.activation_based) {
			contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_ACTIVATION_PROFILE;
		}
		contributor.burst_avg_cycles = profile.burst_avg_cycles;
		contributor.sample_count = profile.sample_count;
		contributor.event_count = profile.event_count;
		contributor.confidence = profile.confidence;
	}

	ret = k_thread_arrival_stats_get((k_tid_t)thread, &stats, false);
	if ((ret == 0) && (stats.count > 0U)) {
		has_arrival = true;
		contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_ARRIVAL;
		contributor.arrival_count = stats.count;
		contributor.arrival_source_mask = diagnostic_arrival_source_mask(stats.source_mask);
	}

	if (!runnable && !has_arrival) {
		return;
	}

	contributor.thread_id = (uintptr_t)thread;
	contributor.thread_name = k_thread_name_get((k_tid_t)thread);

	if (runnable && has_profile &&
	    (profile.confidence >= CONFIG_CPU_WORKLOAD_READY_BACKLOG_MIN_CONFIDENCE)) {
		contributor.flags |= CPU_WORKLOAD_CONTRIBUTOR_READY_BACKLOG;
		contributor.backlog_cycles = profile.burst_avg_cycles;
	}

	if (has_arrival && has_profile) {
		contributor.arrival_cycles = (uint64_t)profile.burst_avg_cycles * stats.count;
	}

	contributor.merged_cycles = MAX(contributor.backlog_cycles, contributor.arrival_cycles);

	contributor_append(ctx, &contributor);
}

#endif /* CONFIG_CPU_WORKLOAD_DIAGNOSTIC */

int cpu_workload_contributors_get(int cpu_id, struct cpu_workload_contributor *contributors,
					  size_t *contributor_count)
{
#if defined(CONFIG_CPU_WORKLOAD_DIAGNOSTIC)
	struct contributor_context ctx;

	CHECKIF((contributors == NULL) || (contributor_count == NULL) ||
		(*contributor_count == 0U) || (cpu_id < 0) || (cpu_id >= arch_num_cpus())) {
		return -EINVAL;
	}

	ctx.contributors = contributors;
	ctx.capacity = *contributor_count;
	ctx.count = 0U;
	ctx.truncated = false;

	k_thread_foreach_filter_by_cpu((unsigned int)cpu_id, contributor_cb, &ctx);
	*contributor_count = ctx.count;

	return ctx.truncated ? -ENOMEM : 0;
#else
	ARG_UNUSED(cpu_id);
	ARG_UNUSED(contributors);

	if (contributor_count != NULL) {
		*contributor_count = 0U;
	}

	return -ENOTSUP;
#endif
}
