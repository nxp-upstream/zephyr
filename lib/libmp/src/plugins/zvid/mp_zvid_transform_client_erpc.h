/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for zvid transform client element which uses eRPC.
 */

#ifndef __MP_ZVID_TRANSFORM_CLIENT_ERPC_H__
#define __MP_ZVID_TRANSFORM_CLIENT_ERPC_H__

struct mp_element;

/**
 * @brief Initialize a video transform element on the client side using eRPC.
 *
 * @param self Pointer to the @ref struct mp_element to initialize as a video transform
 */
void mp_zvid_transform_client_erpc_init(struct mp_element *self);

#endif /* __MP_ZVID_TRANSFORM_CLIENT_ERPC_H__ */
