/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief RPC channel: what a client element calls its server through.
 * @ingroup mpipe_rpc_channel
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_RPC_CHANNEL_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_RPC_CHANNEL_H_

/**
 * @defgroup mpipe_rpc_channel RPC channel
 * @ingroup mpipe
 * @brief The channel between a client element and its server on another core.
 *
 * A client element forwards its work to a server. The channel is the object it
 * calls the server through: each client element has a channel of its own, so
 * a pipeline can hold several client elements of the same kind, each with its
 * own server.
 *
 * The calls of this API are the ones every client element makes, whatever it
 * carries. What depends on the kind of data, the formats above all, is called
 * through the functions of each plugin, which take the channel as well.
 *
 * A channel is provided by an RPC backend, or by the application.
 *
 * @{
 */

#include <stdint.h>

#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_element.h>

struct mpipe_rpc_channel;

/**
 * @brief Functions an RPC channel implements
 */
struct mpipe_rpc_channel_api {
	/**
	 * @brief Connect to the server
	 *
	 * @param channel The channel
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*open)(struct mpipe_rpc_channel *channel);
	/**
	 * @brief Have the server process one buffer
	 *
	 * A transform gives both buffers, a sink the input only, and a source
	 * the output only: the server then fills it with the next buffer of its
	 * own source.
	 *
	 * @param channel The channel
	 * @param in_buf Address of the input buffer, 0 when the client is a source
	 * @param in_sz Number of valid bytes in the input buffer
	 * @param out_buf Address of the output buffer, 0 when the server returns none
	 * @param out_cap Size of the output buffer in bytes
	 * @param[out] out_sz Number of bytes written to the output buffer, 0 when
	 *                    the server produced nothing for this input. NULL when
	 *                    the server returns no buffer.
	 *
	 * @retval 0 Success
	 * @retval -EAGAIN For a source, the server had no buffer within the time
	 *                 it waits: the client asks again
	 * @retval -ENODATA For a source, the stream of the server ended
	 * @return Another negative errno on failure
	 */
	int (*process)(struct mpipe_rpc_channel *channel, uintptr_t in_buf, uint32_t in_sz,
		       uintptr_t out_buf, uint32_t out_cap, uint32_t *out_sz);
	/**
	 * @brief Take the server through a state transition, optional
	 *
	 * @param channel The channel
	 * @param transition The transition the client element goes through
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*set_state)(struct mpipe_rpc_channel *channel, enum mpipe_state_change transition);
	/**
	 * @brief Tell the server the stream ended, optional
	 *
	 * Returns once the server has flushed what it held.
	 *
	 * @param channel The channel
	 *
	 * @return 0 on success, a negative errno on failure
	 */
	int (*eos)(struct mpipe_rpc_channel *channel);
};

/**
 * @brief RPC channel
 *
 * An implementation embeds this structure in its own.
 */
struct mpipe_rpc_channel {
	/** Functions of the implementation */
	const struct mpipe_rpc_channel_api *api;
	/**
	 * Held around mpipe_rpc_channel_process(), mpipe_rpc_channel_set_state()
	 * and mpipe_rpc_channel_eos(). The thread that processes buffers and
	 * the thread that changes the state both call the server, and the
	 * server takes one call at a time.
	 */
	struct k_mutex lock;
};

/**
 * @brief Initialize a channel
 *
 * @param channel The channel
 * @param api Functions of the implementation
 */
void mpipe_rpc_channel_init(struct mpipe_rpc_channel *channel,
			    const struct mpipe_rpc_channel_api *api);

/**
 * @brief Connect a channel to its server
 *
 * @param channel The channel
 *
 * @return 0 on success, a negative errno on failure
 */
int mpipe_rpc_channel_open(struct mpipe_rpc_channel *channel);

/**
 * @brief Have the server process one buffer
 *
 * See @ref mpipe_rpc_channel_api.process for the parameters.
 *
 * @return 0 on success, a negative errno on failure
 */
int mpipe_rpc_channel_process(struct mpipe_rpc_channel *channel, uintptr_t in_buf, uint32_t in_sz,
			      uintptr_t out_buf, uint32_t out_cap, uint32_t *out_sz);

/**
 * @brief Take the server through a state transition
 *
 * @param channel The channel
 * @param transition The transition the client element goes through
 *
 * @return 0 on success or when the channel has no such function, a negative
 *         errno on failure
 */
int mpipe_rpc_channel_set_state(struct mpipe_rpc_channel *channel,
				enum mpipe_state_change transition);

/**
 * @brief Tell the server the stream ended
 *
 * @param channel The channel
 *
 * @return 0 on success or when the channel has no such function, a negative
 *         errno on failure
 */
int mpipe_rpc_channel_eos(struct mpipe_rpc_channel *channel);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_RPC_CHANNEL_H_ */
