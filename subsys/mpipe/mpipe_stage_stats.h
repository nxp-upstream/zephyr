/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Per-second statistics of one pipeline stage, for the Android link sample
 * performance investigation. Temporary: every user sits behind a local
 * MPIPE_STAGE_STATS switch that follows CONFIG_MPIPE_STAGE_STATS, and the
 * file goes once the bottleneck is known.
 *
 * A stage folds one sample in per unit of work; when the window is over the
 * call returns true, the stage prints its line and resets the window.
 */

#ifndef ZEPHYR_SUBSYS_MPIPE_MPIPE_STAGE_STATS_H_
#define ZEPHYR_SUBSYS_MPIPE_MPIPE_STAGE_STATS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define MPIPE_STAGE_STATS_WINDOW_MS 1000U

struct mpipe_stage_stats {
	/* k_uptime_get_32() at the first sample of the window */
	uint32_t window_start;
	/* Window length when the last add() returned true */
	uint32_t elapsed_ms;
	/* Units of work in the window */
	uint32_t count;
	/* Bytes over the window, and the largest single item */
	uint32_t bytes;
	uint32_t bytes_max;
	/* Time per sample, in microseconds */
	uint32_t us_sum;
	uint32_t us_min;
	uint32_t us_max;
	/* Whatever the stage wants to count on the side (drops, failures) */
	uint32_t aux;
};

static inline uint32_t mpipe_stage_stats_elapsed_us(uint32_t start_cycles)
{
	return k_cyc_to_us_near32(k_cycle_get_32() - start_cycles);
}

/* Fold a sample of @p n items and @p bytes bytes that took @p us in */
static inline bool mpipe_stage_stats_add(struct mpipe_stage_stats *s, uint32_t n, uint32_t bytes,
					 uint32_t bytes_max, uint32_t us)
{
	uint32_t now = k_uptime_get_32();

	if (s->window_start == 0U) {
		s->window_start = now;
		s->us_min = UINT32_MAX;
	}

	s->count += n;
	s->bytes += bytes;
	s->bytes_max = MAX(s->bytes_max, bytes_max);
	s->us_sum += us;
	s->us_min = MIN(s->us_min, us);
	s->us_max = MAX(s->us_max, us);
	s->elapsed_ms = now - s->window_start;

	return s->elapsed_ms >= MPIPE_STAGE_STATS_WINDOW_MS;
}

static inline uint32_t mpipe_stage_stats_us_avg(const struct mpipe_stage_stats *s)
{
	return (s->count != 0U) ? s->us_sum / s->count : 0U;
}

/* Per-second rate of @p value over the window that just ended */
static inline uint32_t mpipe_stage_stats_per_s(const struct mpipe_stage_stats *s, uint32_t value)
{
	return (s->elapsed_ms != 0U) ? (uint32_t)(((uint64_t)value * 1000U) / s->elapsed_ms) : 0U;
}

static inline void mpipe_stage_stats_reset(struct mpipe_stage_stats *s)
{
	*s = (struct mpipe_stage_stats){0};
}

#endif /* ZEPHYR_SUBSYS_MPIPE_MPIPE_STAGE_STATS_H_ */
