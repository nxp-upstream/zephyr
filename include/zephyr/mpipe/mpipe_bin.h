/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_bin.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_

/**
 * @defgroup mpipe_bin Bins
 * @ingroup mpipe_framework
 * @brief Container elements that hold and manage child elements.
 * @{
 */

#include <stdint.h>

#include <zephyr/sys/dlist.h>

#include <zephyr/mpipe/mpipe_bus.h>
#include <zephyr/mpipe/mpipe_element.h>

/**
 * @brief Bin structure
 *
 * A container element that can hold multiple child elements.
 *
 * A bin manages the state changes of its children and handles the topology
 * of the pipeline elements within it.
 */
struct mpipe_bin {
	/** Base element structure */
	struct mpipe_element element;
	/** Message bus to communicate with application */
	struct mpipe_bus bus;
	/** Number of children in the bin */
	int children_num;
	/** List of children elements in the bin */
	sys_dlist_t children;
};

/**
 * @brief Initialize a bin
 *
 * Initializes the bin structure and sets up the necessary function pointers
 * and data structures.
 *
 * @param self Pointer to the @ref mpipe_element to initialize as a bin
 */
int mpipe_bin_init(struct mpipe_bin *bin, uint8_t id);

/**
 * @brief Add elements to a bin
 *
 * Adds the given element(s) to the bin.
 *
 * An element can only be added to one bin. Element names must be unique within the bin.
 *
 * The function accepts a variable number of elements, terminated by NULL.
 *
 * If the element's pads are linked to other pads, the pads will be unlinked
 * before the element is added to the bin.
 *
 * @param bin Pointer to the @ref mpipe_bin to add elements to
 * @param element First @ref mpipe_element to add
 * @param ... Additional mpipe_element pointers, terminated by NULL
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_bin_add(struct mpipe_bin *bin, struct mpipe_element *element, ...);

/**
 * @brief Bin state change function
 *
 * Handles state changes for the bin by propagating the state change to all
 * child elements in the appropriate order. The bin manages the topology
 * and ensures proper sequencing of state changes.
 *
 * @param element Pointer to the @ref mpipe_element (bin) changing state
 * @param transition The state transition being performed
 *
 * @return State change return value indicating success, failure, or async operation
 */
enum mpipe_state_change_return mpipe_bin_change_state_func(struct mpipe_element *element,
							   enum mpipe_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_BIN_H_ */
