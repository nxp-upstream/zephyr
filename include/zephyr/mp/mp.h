/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Umbrella header.
 *
 * Applications include this header, and only this one, for the whole core
 * API; each plugin element they instantiate adds that element's own header.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_H_
#define ZEPHYR_INCLUDE_MP_MP_H_

/**
 * @defgroup mp MultimediaPipe
 * @ingroup os_services
 * @brief MultimediaPipe (MP) subsystem.
 * @since 4.5
 */

/**
 * @defgroup mp_framework Framework
 * @ingroup mp
 * @brief Core MultimediaPipe APIs.
 */

/**
 * @defgroup mp_plugins Plugins
 * @ingroup mp
 * @brief MultimediaPipe plugins.
 */

#include <zephyr/mp/mp_bin.h>
#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_bus.h>
#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_message.h>
#include <zephyr/mp/mp_object.h>
#include <zephyr/mp/mp_pad.h>
#include <zephyr/mp/mp_parser.h>
#include <zephyr/mp/mp_pipeline.h>
#include <zephyr/mp/mp_sink.h>
#include <zephyr/mp/mp_src.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_transform.h>
#include <zephyr/mp/mp_value.h>
#if CONFIG_MP_RPC
#include <zephyr/mp/mp_transform_client.h>
#endif

#endif /* ZEPHYR_INCLUDE_MP_MP_H_ */
