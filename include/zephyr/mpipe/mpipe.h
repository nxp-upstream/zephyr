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

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_H_

/**
 * @defgroup mpipe Multimedia Pipeline
 * @ingroup os_services
 * @brief Multimedia Pipeline (mpipe) subsystem.
 * @since 4.5
 */

/**
 * @defgroup mpipe_framework Framework
 * @ingroup mpipe
 * @brief Core Multimedia Pipeline APIs.
 */

/**
 * @defgroup mpipe_plugins Plugins
 * @ingroup mpipe
 * @brief Multimedia Pipeline plugins.
 */

#include <zephyr/mpipe/mpipe_bin.h>
#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/mpipe_object.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_parser.h>
#include <zephyr/mpipe/mpipe_pipeline.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_src.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_transform.h>
#include <zephyr/mpipe/mpipe_value.h>
#if CONFIG_MPIPE_RPC
#include <zephyr/mpipe/mpipe_transform_client.h>
#endif

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_H_ */
