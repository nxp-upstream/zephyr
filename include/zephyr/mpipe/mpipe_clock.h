/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pipeline clock: the time source a pipeline derives its running time from.
 * @ingroup mpipe_clock
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_CLOCK_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_CLOCK_H_

/**
 * @defgroup mpipe_clock Clocks
 * @ingroup mpipe_framework
 * @brief The time source of a pipeline.
 *
 * A clock provides monotonic absolute time in microseconds with an arbitrary
 * epoch. The pipeline derives its running time from it: the time spent in
 * PLAYING, which is what buffer timestamps are expressed in and what
 * synchronized rendering compares against.
 *
 * The provider is pluggable: the default counts kernel uptime, and an
 * audio-driven provider can replace it for audio-master synchronization
 * without any API change.
 *
 * @{
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mpipe_clock;

/**
 * @brief Operations a clock provider implements
 */
struct mpipe_clock_ops {
	/**
	 * @brief Absolute clock time
	 *
	 * Must be monotonic. The epoch is arbitrary; only differences carry
	 * meaning.
	 *
	 * @param clock The clock
	 * @return Time in microseconds
	 */
	uint64_t (*get_time)(struct mpipe_clock *clock);
};

/**
 * @brief A clock instance
 */
struct mpipe_clock {
	/** Operations */
	const struct mpipe_clock_ops *ops;
	/** Provider private state */
	void *priv;
};

/**
 * @brief Read a clock
 *
 * @param clock The clock
 * @return Time in microseconds, 0 when @p clock is NULL or has no provider
 */
uint64_t mpipe_clock_get_time(struct mpipe_clock *clock);

/**
 * @brief The default clock, counting kernel uptime
 *
 * @return The monotonic system clock
 */
struct mpipe_clock *mpipe_clock_monotonic(void);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_CLOCK_H_ */
