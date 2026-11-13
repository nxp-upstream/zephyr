/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Transform client: a transform whose work runs on another core.
 * @ingroup mpipe_transform
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_

/**
 * @addtogroup mpipe_transform
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief Transform client element structure
 *
 * Base structure for all transform elements on the client side of multi-core pipeline.
 * These elements call RPC to the server side to off-load the processing work.
 *
 * The server may run one function or a whole pipeline: the client is one
 * transform either way, and each buffer crosses to the other core once.
 * Every hook is synchronous. A queue in front of the element is what lets the
 * elements upstream work while the other core does.
 *
 * An input that is not in the element's input pool, because nothing upstream
 * adopted the pool, is copied into it first: the other core reads the payload
 * by address, so it must sit in the memory that pool allocates from.
 */
struct mpipe_transform_client {
	/** Base transform structure */
	struct mpipe_transform transform;

	/**
	 * @brief Initialize RPC communication on the client side
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*init_rpc)(void);
	/**
	 * @brief RPC processing function for processing buffers on the server side
	 *
	 * @param in_buf Address of the input buffer to be processed
	 * @param in_sz Number of valid bytes in the input buffer
	 * @param out_buf Address of the output buffer to fill
	 * @param[out] out_sz Number of bytes written to the output buffer, 0 when
	 *                    the server produced nothing for this input
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*process_fn_rpc)(uintptr_t in_buf, uint32_t in_sz, uintptr_t out_buf,
			      uint32_t *out_sz);
	/**
	 * @brief Take the server through a state transition, optional
	 *
	 * Called for every transition but READY to PAUSED: a server that
	 * negotiates does so when the element applies its formats, which
	 * happens later in that transition.
	 *
	 * @param transition The transition the element goes through
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*set_state_rpc)(enum mpipe_state_change transition);
	/**
	 * @brief Tell the server the stream ended, optional
	 *
	 * Returns once the server has flushed what it held, so that the end
	 * of stream goes on downstream after the last buffer.
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*eos_rpc)(void);
	/** The base element's event handler, called after the server heard the event */
	int (*base_event_fn)(struct mpipe_pad *pad, struct mpipe_dispatch *event);
};

/**
 * @brief Initialize a transform client element
 *
 * Initializes the base transform element and installs the processing function that
 * forwards a buffer over the transport instead of processing it locally.
 *
 * @c init_rpc must already be set when this is called: it runs before the base
 * element is initialized, so a transport that cannot come up is reported
 * through the return value and nothing is left half-built.
 *
 * @param transform_client Pointer to the @ref mpipe_transform_client to
 *                         initialize.
 *
 * @param id               Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_transform_client_init(struct mpipe_transform_client *transform_client, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_ */
