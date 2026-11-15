/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pipelines described in the devicetree.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_DT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_DT_H_

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pipeline.h>
#if defined(CONFIG_MPIPE_PLAYER)
#include <zephyr/mpipe/utils/mpipe_player.h>
#endif

/**
 * @defgroup mpipe_dt Pipelines described in the devicetree
 * @ingroup mpipe
 * @since 4.5
 * @version 0.1.0
 *
 * A node compatible with @c zephyr,mpipe-pipeline describes a pipeline: each of
 * its children is an element, and the @c sources property of an element names
 * the sibling it receives its buffers from. The pipeline is built at boot, with
 * no application code, and played when the node has the @c autostart property.
 *
 * An element type supports this by defining, for each of its nodes, the element
 * and an entry with @ref MPIPE_DT_INST_ELEMENT_DEFINE.
 *
 * @{
 */

struct mpipe_dt_element;

/**
 * @brief Function initializing the element of an entry and applying the
 * properties of its node.
 *
 * @param dt Entry of the element.
 *
 * @retval 0 Success
 * @retval -errno The initialization or a property failed
 */
typedef int (*mpipe_dt_element_init_t)(const struct mpipe_dt_element *dt);

/** @brief Entry of an element described in the devicetree. */
struct mpipe_dt_element {
	/** Element of the node */
	struct mpipe_element *element;
	/** Initializes the element and applies the properties of the node */
	mpipe_dt_element_init_t init;
	/** Properties of the node, in the form the element type defines */
	const void *config;
	/** Entry of the upstream element, NULL for a source */
	const struct mpipe_dt_element *source;
	/** Name of the node */
	const char *name;
	/** Identifier of the element in its pipeline */
	uint8_t id;
};

/** @cond INTERNAL_HIDDEN */

#define Z_MPIPE_DT_ELEMENT_NAME(node_id)  _CONCAT(mpipe_dt_element_, DT_DEP_ORD(node_id))
#define Z_MPIPE_DT_PIPELINE_NAME(node_id) _CONCAT(mpipe_dt_pipeline_, DT_DEP_ORD(node_id))
#define Z_MPIPE_DT_PLAYER_NAME(node_id)   _CONCAT(mpipe_dt_player_, DT_DEP_ORD(node_id))

#define Z_MPIPE_DT_ELEMENT_DECLARE(node_id)                                                        \
	extern const struct mpipe_dt_element Z_MPIPE_DT_ELEMENT_NAME(node_id);

#if defined(CONFIG_MPIPE_PLAYER)
#define Z_MPIPE_DT_PLAYER_DECLARE(node_id)                                                         \
	extern struct mpipe_player Z_MPIPE_DT_PLAYER_NAME(node_id);
#else
#define Z_MPIPE_DT_PLAYER_DECLARE(node_id)
#endif

#define Z_MPIPE_DT_PIPELINE_DECLARE(node_id)                                                       \
	extern struct mpipe Z_MPIPE_DT_PIPELINE_NAME(node_id);                                     \
	Z_MPIPE_DT_PLAYER_DECLARE(node_id)                                                         \
	DT_FOREACH_CHILD_STATUS_OKAY(node_id, Z_MPIPE_DT_ELEMENT_DECLARE)

DT_FOREACH_STATUS_OKAY(zephyr_mpipe_pipeline, Z_MPIPE_DT_PIPELINE_DECLARE)

/** @endcond */

/**
 * @brief Define the entry of an element node.
 *
 * @param node_id Node of the element.
 * @param element_ptr Element of the node, a pointer to an object whose first
 *                    member is a struct mpipe_element.
 * @param init_fn Function initializing the element, an
 *                @ref mpipe_dt_element_init_t.
 * @param config_ptr Properties of the node handed to @p init_fn, or NULL.
 */
#define MPIPE_DT_ELEMENT_DEFINE(node_id, element_ptr, init_fn, config_ptr)                         \
	BUILD_ASSERT(DT_PROP_LEN_OR(node_id, sources, 0) <= 1,                                     \
		     "An element with several sink pads is not supported");                        \
	const struct mpipe_dt_element Z_MPIPE_DT_ELEMENT_NAME(node_id) = {                         \
		.element = (struct mpipe_element *)(element_ptr),                                  \
		.init = (init_fn),                                                                 \
		.config = (config_ptr),                                                            \
		.source = COND_CODE_1(DT_NODE_HAS_PROP(node_id, sources),                          \
				      (&Z_MPIPE_DT_ELEMENT_NAME(                                   \
					      DT_PHANDLE_BY_IDX(node_id, sources, 0))),            \
				      (NULL)),                                                     \
		.name = DT_NODE_FULL_NAME(node_id),                                                \
		.id = DT_NODE_CHILD_IDX(node_id) + 1,                                              \
	}

/**
 * @brief Define the entry of an instance of the element type @c DT_DRV_COMPAT.
 *
 * @param inst Instance number.
 * @param ... Other arguments of @ref MPIPE_DT_ELEMENT_DEFINE.
 */
#define MPIPE_DT_INST_ELEMENT_DEFINE(inst, ...)                                                    \
	MPIPE_DT_ELEMENT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

/**
 * @brief Get the device a phandle property of an instance names.
 *
 * @param inst Instance number.
 * @param prop Lowercase-and-underscores name of the phandle property.
 *
 * @return Pointer to the device, NULL when the node has no such property.
 */
#define MPIPE_DT_INST_DEVICE_GET_OR_NULL(inst, prop)                                               \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, prop),                                             \
		    (DEVICE_DT_GET(DT_INST_PHANDLE(inst, prop))), (NULL))

/**
 * @brief Get the element of a node.
 *
 * It is initialized, added to its pipeline and linked once the pipelines are
 * built, at the @c APPLICATION initialization level.
 *
 * @param node_id Node of the element.
 *
 * @return Pointer to the struct mpipe_element of the node.
 */
#define MPIPE_DT_ELEMENT_GET(node_id) (Z_MPIPE_DT_ELEMENT_NAME(node_id).element)

/**
 * @brief Get the pipeline of a node.
 *
 * @param node_id Node of the pipeline.
 *
 * @return Pointer to the struct mpipe of the node.
 */
#define MPIPE_DT_PIPELINE_GET(node_id) (&Z_MPIPE_DT_PIPELINE_NAME(node_id))

/**
 * @brief Get the player of a pipeline node.
 *
 * @kconfig_dep{CONFIG_MPIPE_PLAYER}
 *
 * @param node_id Node of the pipeline.
 *
 * @return Pointer to the struct mpipe_player of the node.
 */
#define MPIPE_DT_PLAYER_GET(node_id) (&Z_MPIPE_DT_PLAYER_NAME(node_id))

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_DT_H_ */
