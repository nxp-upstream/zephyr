/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_aud
 * @brief Raw PCM parser element.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_RAW_PARSER_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_RAW_PARSER_H_

#include <zephyr/kernel.h>
#include <zephyr/mpipe/mpipe_parser.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool.h>

/** @addtogroup mpipe_aud */
/** @{ */

/**
 * @brief Raw PCM parser property identifiers.
 *
 * The format properties describe a stream that cannot describe itself; they
 * are settable in @ref MPIPE_STATE_READY only, since the capability they build
 * is negotiated on READY to PAUSED.
 */
enum mpipe_prop_aud_raw_parser {
	/** Memory slab the output blocks come from (struct k_mem_slab pointer, by value). */
	MPIPE_PROP_AUD_RAW_PARSER_SLAB_PTR = MPIPE_PROP_PARSER_LAST,
	/** Sample rate in Hz (const uint32_t pointer). */
	MPIPE_PROP_AUD_RAW_PARSER_SAMPLE_RATE,
	/** Sample width in bits, 16, 24 or 32 (const uint32_t pointer). */
	MPIPE_PROP_AUD_RAW_PARSER_BITWIDTH,
	/** Number of interleaved channels (const uint32_t pointer). */
	MPIPE_PROP_AUD_RAW_PARSER_NUM_CHANNELS,
	/** Time one output block covers, in microseconds (const uint32_t pointer). */
	MPIPE_PROP_AUD_RAW_PARSER_FRAME_INTERVAL,
};

/**
 * @brief Raw PCM parser element.
 *
 * Extends @ref mpipe_parser. A raw PCM stream, from a socket or a headerless
 * file, carries neither its format nor any framing. This element announces
 * the format set by its properties and cuts the bytes into blocks of the
 * duration the element downstream negotiated, each a whole number of sample
 * frames, taken from the audio buffer pool so that an I2S sink can hand them
 * to its driver as they are. The bytes are copied once, into the block.
 */
struct mpipe_aud_raw_parser {
	/** Base parser element */
	struct mpipe_parser base;
	/** Pool the output blocks come from */
	struct mpipe_aud_buffer_pool out_pool;
	/** Block being filled, or NULL */
	struct net_buf *block;
	/** Sample rate in Hz */
	uint32_t sample_rate;
	/** Sample width in bits */
	uint32_t bit_width;
	/** Number of channels */
	uint32_t num_of_channel;
	/** Time one block covers, in microseconds */
	uint32_t frame_interval;
};

/**
 * @brief Initialize a raw PCM parser element.
 *
 * The format defaults to 48 kHz, 16-bit, stereo, in blocks of 10 ms.
 *
 * @param parser Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_aud_raw_parser_init(struct mpipe_aud_raw_parser *parser, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_RAW_PARSER_H_ */
