/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_transform_client.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_

/**
 * @ingroup mpipe_transform
 * @brief Client-side transform elements that offload processing over RPC.
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief Transform client element structure
 *
 * Base structure for all transform elements on the client side of multi-core pipeline.
 * These elements call RPC to the server side to off-load the processing work.
 */
struct mpipe_transform_client {
	/** Base transform structure */
	struct mpipe_transform transform;

	/**
	 * @brief Initialize RPC communication on the client side
	 *
	 * @return 0 on success, an errno on failure
	 */
	int (*init_rpc)(void);
	/**
	 * @brief RPC chain function for processing buffers on the server side
	 *
	 * @param in_buf Address of the input buffer to be processed
	 * @param in_sz Input buffer size
	 * @param out_buf Address of the processed output buffer
	 * @param out_sz Output buffer size
	 * @return 0 on success, an errno on failure
	 */
	int (*chain_fn_rpc)(uint32_t in_buf, uint32_t in_sz, uint32_t out_buf, uint32_t *out_sz);
	/** Application-set input pool config, restored before each proposal */
	struct mpipe_buffer_pool_config in_pool_base;
	/** Application-set output pool config, restored at each negotiation */
	struct mpipe_buffer_pool_config out_pool_base;
	/** Whether in_pool_base has been captured yet */
	bool in_pool_base_valid;
	/** Whether out_pool_base has been captured yet */
	bool out_pool_base_valid;
};

/**
 * @brief Initialize a transform element
 *
 * This function initializes the base transform element structure,
 * sets up sink and source pads, and configures default function
 * pointers for element operations.
 *
 * @param self Pointer to the element to initialize (@ref mpipe_element)
 */
int mpipe_transform_client_init(struct mpipe_transform_client *transform_client, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_TRANSFORM_CLIENT_H_ */
