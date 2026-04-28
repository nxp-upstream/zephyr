/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mp.h: Main header for MediaPipe core
 *
 * Applications should include this and only this for the core MediaPipe APIs.
 *
 */

#ifndef __MP_H__
#define __MP_H__

#include <zephyr/mp/core/mp_bus.h>
#include <zephyr/mp/core/mp_caps.h>
#if CONFIG_MP_CAPSFILTER
#include <zephyr/mp/core/mp_capsfilter.h>
#endif
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pipeline.h>
#include <zephyr/mp/core/mp_structure.h>
#include <zephyr/mp/core/mp_value.h>
#if CONFIG_MP_RPC
#include <zephyr/mp/core/mp_transform_client.h>
#endif

#endif /* __MP_H__ */
