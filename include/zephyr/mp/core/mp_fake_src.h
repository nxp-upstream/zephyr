/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mp
 * @brief Fake source element header.
 *
 * A synthetic source element for testing media pipelines.
 */

#ifndef ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SRC_H_
#define ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SRC_H_

#include <stdint.h>

#include <zephyr/mp/core/mp_buffer.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_property.h>
#include <zephyr/mp/core/mp_src.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cast a generic pointer to mp_fake_src */
#define MP_FAKE_SRC(self) ((struct mp_fake_src *)(self))

/**
 * @brief Packet size generation mode
 */
enum mp_fake_src_size_mode {
	/** Push zero-byte packets (empty packet mode) */
	MP_FAKE_SRC_SIZE_EMPTY = 0,
	/** Every packet is exactly sizemax bytes (fixed-size mode) */
	MP_FAKE_SRC_SIZE_FIXED,
	/** Packet size varies uniformly in [sizemin, sizemax] (random-size mode) */
	MP_FAKE_SRC_SIZE_RANDOM,
};

/**
 * @brief Packet payload fill mode
 */
enum mp_fake_src_fill_mode {
	/** Fill packet memory with zeroes */
	MP_FAKE_SRC_FILL_ZERO = 0,
	/** Leave packet memory untouched (no-op fill) */
	MP_FAKE_SRC_FILL_NONE,
};

/**
 * @brief Properties specific to mp_fake_src
 */
enum prop_fake_src {
	/** Size generation mode: @ref mp_fake_src_size_mode */
	PROP_FAKE_SRC_SIZE_MODE = PROP_SRC_LAST,
	/** Minimum packet size in bytes (used in RANDOM mode) */
	PROP_FAKE_SRC_SIZE_MIN,
	/** Maximum / fixed packet size in bytes */
	PROP_FAKE_SRC_SIZE_MAX,
	/** Datarate in bytes/second (0 = no timestamp derivation) */
	PROP_FAKE_SRC_DATA_RATE,
	/** Fill mode: @ref mp_fake_src_fill_mode */
	PROP_FAKE_SRC_FILL_MODE,
	/** Sentinel — must be last */
	PROP_FAKE_SRC_LAST,
};

/**
 * @brief Fake source element
 */
struct mp_fake_src {
	/** Base source element (must be first) */
	struct mp_src base;

	/** Packet size mode */
	enum mp_fake_src_size_mode size_mode;
	/** Minimum packet size */
	uint32_t size_min;
	/** Maximum packet size */
	uint32_t size_max;
	/** Datarate in bytes/second for timestamp derivation (0 = disabled) */
	uint32_t data_rate;
	/** Content fill mode */
	enum mp_fake_src_fill_mode fill_mode;

	/** Total bytes pushed downstream since last start */
	uint64_t bytes_sent;
	/** Number of packets pushed since last start */
	uint32_t packets_sent;

	/** Payload buffer. */
	uint8_t data_buf[CONFIG_MP_FAKE_SRC_BUF_SIZE];

	/** Buffer pool for managing output buffers */
	struct mp_buffer_pool pool;
};

/**
 * @brief Initialize a fake source element
 *
 * Initialises the base @ref mp_src, sets up the internal buffer pool,
 * wires all callbacks, and applies default configuration values.
 *
 * @param self Pointer to the @ref mp_element to initialise as a fake source
 */
void mp_fake_src_init(struct mp_element *self);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MP_CORE_MP_FAKE_SRC_H_ */
