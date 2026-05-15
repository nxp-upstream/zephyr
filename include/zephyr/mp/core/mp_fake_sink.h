/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Fake sink element header.
 *
 * mp_fake_sink is a simulated sink element for testing mp pipelines.
 *
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SINK_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SINK_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/mp/core/mp_buffer.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_sink.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cast a generic pointer to mp_fake_sink */
#define MP_FAKE_SINK(self) ((struct mp_fake_sink *)(self))

/**
 * @brief Per-buffer processing callback.
 *
 * Invoked synchronously inside chainfn, before net_buf_unref,
 * when on_buffer != NULL.
 *
 * @param data Read-only pointer to in_buf->data (invalid after return).
 * @param size in_buf->len (bytes valid in the payload).
 * @param timestamp Buffer timestamp in milliseconds (from mp_buffer_meta).
 * @param duration Per-packet duration encoded in meta->priv (milliseconds).
 * @param user_ctx Opaque pointer set on the element struct.
 */
typedef void (*mp_fake_sink_on_buffer_fn)(const uint8_t *data, uint32_t size, uint32_t timestamp,
					  uint32_t duration, void *user_ctx);

/**
 * @brief Fake sink element.
 */
struct mp_fake_sink {
	/** Base sink element (MUST be first member). */
	struct mp_sink base;

	/** Per-buffer callback. NULL = disabled. */
	mp_fake_sink_on_buffer_fn on_buffer;

	/** Opaque user pointer passed to all callbacks. */
	void *user_ctx;

	/**
	 * Total buffers consumed since last READY_TO_PAUSED.
	 */
	uint32_t buffers_consumed;

	/**
	 * Total bytes consumed since last READY_TO_PAUSED.
	 */
	uint64_t bytes_consumed;

	/**
	 * Simulated per-buffer processing delay (milliseconds).
	 * Set to 0 (default) for burst / zero-latency consumption.
	 */
	uint32_t processing_delay_ms;
};

/**
 * @brief Initialize a fake sink element.
 *
 * @param self Pointer to the @ref mp_element to initialise as a fake sink.
 */
void mp_fake_sink_init(struct mp_element *self);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SINK_H_ */
