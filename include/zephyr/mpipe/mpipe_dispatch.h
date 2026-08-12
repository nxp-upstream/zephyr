/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Dispatch for event and query handler
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_DISPATCH_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_DISPATCH_H_

/**
 * @defgroup mpipe_dispatch Dispatches
 * @ingroup mpipe_framework
 * @brief Dispatch objects exchanged between elements.
 * @{
 */

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_object.h>

/**
 * @enum mpipe_dispatch_type
 * @brief Supported dispatch types.
 */
enum mpipe_dispatch_type {
	MPIPE_DISPATCH_UNKNOWN = 0,     /**< Unknown dispatch type */
	MPIPE_DISPATCH_EOS,             /**< EOS dispatch type */
	MPIPE_DISPATCH_CAPS,            /**< CAPS dispatch type */
	MPIPE_DISPATCH_BUFFER_CONFIG,   /**< Buffer config type */
	MPIPE_DISPATCH_END = UINT8_MAX, /**< Maximum dispatch type identifier */
};

/**
 * @struct mpipe_dispatch
 * @brief Structure representing a dispatch (event or query).
 */
struct mpipe_dispatch {
	uint8_t type;                   /**< Dispatch type from @ref mpipe_dispatch_type */
	struct mpipe_structure caps;    /**< The capability carried, ANY when none */
	struct mpipe_buffer_pool *pool; /**< Buffer pool (not owned) */
	struct mpipe_buffer_pool_config pool_cfg; /**< Pool config */
};

/**
 * @brief Initialize a dispatch of any type.
 *
 * @param dispatch Pointer to a @ref mpipe_dispatch to initialize.
 * @param type     Dispatch type from @ref mpipe_dispatch_type.
 * @param caps     Capability to copy in, or NULL for one that constrains nothing.
 */
void mpipe_dispatch_init(struct mpipe_dispatch *dispatch, uint8_t type,
			 const struct mpipe_structure *caps);

/**
 * @brief Initialize a dispatch with end-of-stream type.
 *
 * @param d Pointer to the @ref mpipe_dispatch structure to initialize.
 */
#define mpipe_dispatch_eos_init(d) mpipe_dispatch_init(d, MPIPE_DISPATCH_EOS, NULL)

/**
 * @brief Initialize a dispatch with caps type.
 *
 * @param d Pointer to the @ref mpipe_dispatch structure to initialize.
 * @param c Pointer to the @ref mpipe_structure to copy in, or NULL.
 */
#define mpipe_dispatch_caps_init(d, c) mpipe_dispatch_init(d, MPIPE_DISPATCH_CAPS, c)

/**
 * @brief Initialize a dispatch with buffer-configuration type.
 *
 * @param d Pointer to the @ref mpipe_dispatch structure to initialize.
 * @param c Pointer to the @ref mpipe_structure to copy in, or NULL.
 */
#define mpipe_dispatch_buffer_config_init(d, c)                                                    \
	mpipe_dispatch_init(d, MPIPE_DISPATCH_BUFFER_CONFIG, c)

/**
 * @brief Clear a dispatch, releasing any owned resources.
 *
 * @param dispatch Pointer to a @ref mpipe_dispatch to clear.
 */
void mpipe_dispatch_clear(struct mpipe_dispatch *dispatch);

/**
 * @brief Get the capability carried by a dispatch.
 *
 * The dispatch keeps ownership; the pointer is valid until it is cleared or
 * its capability replaced.
 *
 * @param dispatch Pointer to a @ref mpipe_dispatch.
 *
 * @return Pointer to the @ref mpipe_structure, or NULL if @p dispatch is NULL.
 */
struct mpipe_structure *mpipe_dispatch_get_caps(struct mpipe_dispatch *dispatch);

/**
 * @brief Set (replace) the caps on a dispatch.
 *
 * @param dispatch Pointer to a @ref mpipe_dispatch.
 * @param caps     Capability to copy in, or NULL for one that constrains nothing.
 *
 * @retval 0       Success.
 * @retval -EINVAL @p dispatch is NULL or type does not carry caps.
 */
int mpipe_dispatch_set_caps(struct mpipe_dispatch *dispatch, const struct mpipe_structure *caps);

/**
 * @brief Set the buffer pool on an allocation dispatch.
 *
 * @param dispatch Pointer to a @ref MPIPE_DISPATCH_BUFFER_CONFIG dispatch.
 * @param pool     Pointer to @ref mpipe_buffer_pool.
 *
 * @retval 0       Success.
 * @retval -EINVAL @p dispatch is NULL or not an allocation dispatch.
 */
int mpipe_dispatch_set_pool(struct mpipe_dispatch *dispatch, struct mpipe_buffer_pool *pool);

/**
 * @brief Set the pool configuration on an allocation dispatch.
 *
 * @param dispatch Pointer to a @ref MPIPE_DISPATCH_BUFFER_CONFIG dispatch.
 * @param config   Pointer to @ref mpipe_buffer_pool_config.
 *
 * @retval 0       Success.
 * @retval -EINVAL @p dispatch is NULL or not an allocation dispatch.
 */
int mpipe_dispatch_set_pool_config(struct mpipe_dispatch *dispatch,
				   struct mpipe_buffer_pool_config *config);

/**
 * @brief Get the buffer pool from an allocation dispatch.
 *
 * @param dispatch Pointer to a @ref MPIPE_DISPATCH_BUFFER_CONFIG dispatch.
 *
 * @return Pointer to @ref mpipe_buffer_pool, or NULL.
 */
struct mpipe_buffer_pool *mpipe_dispatch_get_pool(struct mpipe_dispatch *dispatch);

/**
 * @brief Get the pool configuration from an allocation dispatch.
 *
 * @param dispatch Pointer to a @ref MPIPE_DISPATCH_BUFFER_CONFIG dispatch.
 *
 * @return Pointer to @ref mpipe_buffer_pool_config, or NULL.
 */
struct mpipe_buffer_pool_config *mpipe_dispatch_get_pool_config(struct mpipe_dispatch *dispatch);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_DISPATCH_H_ */
