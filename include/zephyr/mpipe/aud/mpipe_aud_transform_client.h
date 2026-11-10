/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Client-side audio transform element for multi-core pipelines.
 * @ingroup mpipe_aud_transform_clients
 *
 * Extends the generic transform client with audio-specific RPC callbacks
 * to offload audio processing to a remote core.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_H_

/**
 * @defgroup mpipe_aud_transform_clients Transform Clients
 * @ingroup mpipe_aud
 * @brief Client-side audio transform elements.
 * @{
 */

#include <stdint.h>

#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_transform_client.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool_client.h>

/**
 * @brief Audio functions a remote audio transform server may provide.
 *
 * The client names the function it runs once, when it opens it; how the
 * function works is the server's business. A server refuses the functions it
 * does not have.
 */
enum mpipe_aud_remote_func {
	/** PCM in, PCM scaled by a gain out */
	MPIPE_AUD_REMOTE_FUNC_GAIN = 1,
	/** PCM in, its complex spectrum (@ref MPIPE_MEDIA_AUDIO_FFT) out */
	MPIPE_AUD_REMOTE_FUNC_FFT = 2,
	/** Complex spectrum (@ref MPIPE_MEDIA_AUDIO_FFT) in, PCM out */
	MPIPE_AUD_REMOTE_FUNC_IFFT = 3,
};

/**
 * @brief Client-side audio transform element structure.
 *
 * Extends @ref mpipe_transform_client with RPC function pointers for
 * function selection, capability query, format negotiation, cap
 * transformation and processing across cores.
 *
 * At init the element opens one function on the remote and keeps the handle
 * the remote returns; every later call carries that handle, so several
 * elements can each run their own function, or their own instance of the
 * same one, on a single remote.
 *
 * The buffers on both pads belong to the remote core, so that it only ever
 * touches its own memory. The remote keeps a set of input and a set of
 * output buffers for each instance, and the element owns the two pools that
 * hand them out, as any other element owns its pools. The sink pad proposes
 * the input pool to upstream; a buffer that arrives from any other pool,
 * including the output pool of another such element, is copied into one of
 * its buffers before it is handed over.
 */
struct mpipe_aud_transform_client {
	/** Base transform client structure */
	struct mpipe_transform_client transform;
	/** Input buffer pool, the remote's input buffers of this instance */
	struct mpipe_aud_buffer_pool_client in_pool;
	/** Output buffer pool, the remote's output buffers of this instance */
	struct mpipe_aud_buffer_pool_client out_pool;
	/** Handle of the function instance opened on the remote */
	uint32_t handle;
	/**
	 * @brief RPC callback to open one function on the remote.
	 *
	 * @param func Function to open.
	 * @param[out] handle Handle of the new instance, for every later call.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*open_rpc)(enum mpipe_aud_remote_func func, uint32_t *handle);
	/**
	 * @brief RPC callback to describe the buffers of one direction of an instance.
	 *
	 * @param handle Function instance.
	 * @param direction Pad direction (see @ref mpipe_pad_direction).
	 * @param[out] num_buffers Number of buffers the remote keeps for it.
	 * @param[out] buf_size Size of each buffer, in bytes.
	 * @param[out] buf_align Alignment of the buffers, in bytes.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*get_buf_caps_rpc)(uint32_t handle, enum mpipe_pad_direction direction,
				    uint8_t *num_buffers, uint32_t *buf_size, uint16_t *buf_align);
	/** RPC callback returning the address of one buffer of an instance */
	mpipe_aud_buffer_pool_client_get_buf_t get_buf_rpc;
	/**
	 * @brief RPC callback to enumerate the capabilities of one direction.
	 *
	 * @param handle Function instance.
	 * @param direction Pad direction (see @ref mpipe_pad_direction).
	 * @param ind Index to iterate through available capabilities.
	 * @param[out] caps Audio capabilities at @p ind.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*get_format_caps_rpc)(uint32_t handle, enum mpipe_pad_direction direction,
				       uint8_t ind, struct audio_caps *caps);
	/**
	 * @brief RPC callback to set the audio format of one direction.
	 *
	 * @param handle Function instance.
	 * @param direction Pad direction (see @ref mpipe_pad_direction).
	 * @param caps Audio format to apply: one sample rate bit, one bit width
	 *        bit, and equal minimum and maximum channel counts and frame
	 *        intervals.
	 * @param[out] buf_size Bytes one buffer in that format uses.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*set_format_rpc)(uint32_t handle, enum mpipe_pad_direction direction,
				  const struct audio_caps *caps, uint32_t *buf_size);
	/**
	 * @brief RPC callback to transform capabilities to the opposite pad.
	 *
	 * @param handle Function instance.
	 * @param direction Pad direction to transform to (see @ref mpipe_pad_direction).
	 * @param ind Index to iterate through transformed capabilities.
	 * @param caps Audio capabilities on the other pad.
	 * @param[out] other_caps Transformed audio capabilities.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*transform_cap_rpc)(uint32_t handle, enum mpipe_pad_direction direction,
				     uint16_t ind, const struct audio_caps *caps,
				     struct audio_caps *other_caps);
	/**
	 * @brief RPC callback to process one buffer on the remote.
	 *
	 * @param handle Function instance.
	 * @param in_buf Address of a remote input buffer.
	 * @param in_sz Number of valid bytes in @p in_buf.
	 * @param out_buf Address of a remote output buffer.
	 * @param[out] out_sz Number of bytes written to @p out_buf.
	 *
	 * @return 0 on success or a negative errno code on failure.
	 */
	int32_t (*process_rpc)(uint32_t handle, uint32_t in_buf, uint32_t in_sz, uint32_t out_buf,
			       uint32_t *out_sz);
};

/**
 * @brief Initialize a client-side audio transform element.
 *
 * Every RPC hook of @p atc and @c init_rpc of its base transform client must
 * be set. The element opens @p func on the remote, then asks the remote for
 * the buffers of that instance and sets up its two pools with them.
 *
 * @param atc Pointer to the element to initialize.
 * @param id Unique element identifier.
 * @param func Function the element runs on the remote.
 *
 * @retval 0 Success.
 * @retval -EINVAL A hook is missing.
 * @retval -ENODEV The remote did not report the buffers of the instance.
 * @return Other negative errno from mpipe_transform_client_init(), from
 *         mpipe_aud_buffer_pool_client_setup() or from the remote, which
 *         refuses a function it does not have.
 */
int mpipe_aud_transform_client_init(struct mpipe_aud_transform_client *atc, uint8_t id,
				    enum mpipe_aud_remote_func func);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_H_ */
