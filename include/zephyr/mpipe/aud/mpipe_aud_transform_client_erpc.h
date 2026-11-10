/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Client-side audio transform element over eRPC.
 * @ingroup mpipe_aud_transform_clients
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_H_
#define ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_H_

/**
 * @addtogroup mpipe_aud_transform_clients
 * @{
 */

#include <stdint.h>

#include <zephyr/mpipe/aud/mpipe_aud_transform_client.h>

/**
 * @brief Initialize a client-side audio transform element bound to eRPC.
 *
 * Installs the eRPC hooks on the element, then initializes it as
 * mpipe_aud_transform_client_init() does. The first element initialized binds
 * the audio transform interface to the client the eRPC backend brings up.
 * Every element gets the buffers of its own instance from the remote, and the
 * calls of all of them are serialized.
 *
 * @param atc Pointer to the element to initialize.
 * @param id Unique element identifier.
 * @param func Function the element runs on the remote.
 *
 * @retval 0 Success.
 * @retval -EIO The eRPC transport could not be brought up.
 * @retval -ENODEV The remote did not report its buffers.
 * @return Other negative errno from mpipe_aud_transform_client_init().
 */
int mpipe_aud_transform_client_erpc_init(struct mpipe_aud_transform_client *atc, uint8_t id,
					 enum mpipe_aud_remote_func func);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUD_MPIPE_AUD_TRANSFORM_CLIENT_ERPC_H_ */
