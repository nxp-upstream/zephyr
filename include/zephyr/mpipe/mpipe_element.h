/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mpipe_element.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_MPIPE_ELEMENT_H_
#define ZEPHYR_INCLUDE_MPIPE_MPIPE_ELEMENT_H_

/**
 * @defgroup mpipe_element Elements
 * @ingroup mpipe_framework
 * @brief Common element lifecycle, state, and pad-linking APIs.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/dlist.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_object.h>

struct mpipe_bus;
struct mpipe_dispatch;

/** @cond INTERNAL_HIDDEN */
#if defined(CONFIG_MPIPE_DUMP)
#define MPIPE_ELEMENT_SET_NAME(e, initfn) ((e)->name = #initfn)
#else
#define MPIPE_ELEMENT_SET_NAME(e, initfn) ((void)0)
#endif
/** @endcond */

/**
 * @brief Initialize and configure an element in one step.
 *
 * Calls @ref mpipe_element_init followed by the element-specific init function.
 *
 * When CONFIG_MPIPE_DUMP is enabled, the element is also named after @p initfn so
 * a dump can identify it.
 *
 * @param elem   Pointer to the element to initialize.
 * @param initfn Element-specific initialization function.
 * @param id     Unique element identifier.
 */
#define MPIPE_ELEMENT_INIT(elem, initfn, id)                                                       \
	do {                                                                                       \
		struct mpipe_element *e = (struct mpipe_element *)(elem);                          \
		mpipe_element_init(e, (id));                                                       \
		initfn(e);                                                                         \
		MPIPE_ELEMENT_SET_NAME(e, initfn);                                                 \
	} while (0)

/**
 * @brief Calculate the next state
 *
 * Given a current state and a target state, calculate the next intermediate state
 *
 * @param cur Current state, see @ref mpipe_state
 * @param target Target state, see @ref mpipe_state
 * @return Next intermediate state
 *
 */
#define MPIPE_STATE_GET_NEXT(cur, target)                                                          \
	((enum mpipe_state)((int)cur + SYS_SIGN((int)target - (int)cur)))

/**
 * @brief Create state transition value
 *
 * @param cur Current state
 * @param next Next state
 * @return State transition value
 */
#define MPIPE_STATE_TRANSITION(cur, next) (((cur) << 2) | (next))

/**
 * @brief Extract the current state from the state transition
 *
 * Given a state transition, extract the current state.
 *
 * @param trans A transition state, see @ref mpipe_state_change
 * @return The current state
 *
 */
#define MPIPE_STATE_TRANSITION_CURRENT(trans) ((enum mpipe_state)((trans) >> 2))

/**
 * @brief Extract the next state from the state transition
 *
 * Given a state transition, extract the next state.
 *
 * @param trans A transition state, see @ref mpipe_state_change
 * @return The next state
 *
 */
#define MPIPE_STATE_TRANSITION_NEXT(trans) ((enum mpipe_state)((trans) & 0x3))

/**
 * @brief States of an element
 *
 * All possible states that an element can be in.
 */
enum mpipe_state {
	/** The element is initialized READY to go to PAUSED. */
	MPIPE_STATE_READY = 0,
	/** The element is PAUSED and ready to receive, process or transfer data. */
	MPIPE_STATE_PAUSED = 1,
	/** The element is PLAYING, data is flowing through the element */
	MPIPE_STATE_PLAYING = 2,
};

/**
 * @brief enum mpipe_state_change
 *
 * Different possible state changes that an element can go through.
 */
enum mpipe_state_change {
	/** State change from READY to PAUSED */
	MPIPE_STATE_CHANGE_READY_TO_PAUSED =
		MPIPE_STATE_TRANSITION(MPIPE_STATE_READY, MPIPE_STATE_PAUSED),
	/** State change from PAUSED to PLAYING*/
	MPIPE_STATE_CHANGE_PAUSED_TO_PLAYING =
		MPIPE_STATE_TRANSITION(MPIPE_STATE_PAUSED, MPIPE_STATE_PLAYING),
	/** State change from PLAYING to PAUSED */
	MPIPE_STATE_CHANGE_PLAYING_TO_PAUSED =
		MPIPE_STATE_TRANSITION(MPIPE_STATE_PLAYING, MPIPE_STATE_PAUSED),
	/** State change from PAUSED to READY */
	MPIPE_STATE_CHANGE_PAUSED_TO_READY =
		MPIPE_STATE_TRANSITION(MPIPE_STATE_PAUSED, MPIPE_STATE_READY),
};

/**
 * @brief enum mpipe_state_change_return
 *
 * Possible returned values from a state change function
 */
enum mpipe_state_change_return {
	/** The state change has failed */
	MPIPE_STATE_CHANGE_FAILURE = 0,
	/** The state change has succeeded */
	MPIPE_STATE_CHANGE_SUCCESS = 1,
	/** The state change will happen asynchronously */
	MPIPE_STATE_CHANGE_ASYNC = 2,
};

struct mpipe_element;

/**
 * @brief Element base class
 *
 * The base class for all elements. Elements are the basic building blocks
 * of a multimedia pipeline. They can have source pads (outputs) and sink
 * pads (inputs) and can be linked together to form processing chains.
 */
struct mpipe_element {
	/** Base object */
	struct mpipe_object object;

#if defined(CONFIG_MPIPE_DUMP)
	/**
	 * Name of the element, set by @ref MPIPE_ELEMENT_INIT from the name of the init
	 * function it is given, used for debugging purposes.
	 */
	const char *name;
#endif

	/** List of source pads */
	sys_dlist_t srcpads;
	/** List of sink pads */
	sys_dlist_t sinkpads;

	/** Current state of the element */
	enum mpipe_state current_state;
	/** Next state (for transitions) */
	enum mpipe_state next_state;
	/** Pending state (for async transitions) */
	enum mpipe_state pending_state;
	/** Target state */
	enum mpipe_state target_state;

	/** Event handler function */
	int (*eventfn)(struct mpipe_element *element, struct mpipe_dispatch *event);
	/** Query handler function */
	int (*queryfn)(struct mpipe_element *element, struct mpipe_dispatch *query);

	/** Get current state function */
	enum mpipe_state_change_return (*get_state)(struct mpipe_element *element,
						    enum mpipe_state *state);
	/** Set state function */
	enum mpipe_state_change_return (*set_state)(struct mpipe_element *element,
						    enum mpipe_state state);
	/** Change state function */
	enum mpipe_state_change_return (*change_state)(struct mpipe_element *element,
						       enum mpipe_state_change transition);
};

/**
 * @brief Initialize an element
 *
 * Initializes the base @ref mpipe_element structure.
 *
 * @param self Pointer to the @ref mpipe_element to initialize.
 * @param id   Unique element identifier.
 */
void mpipe_element_init(struct mpipe_element *self, uint8_t id);

struct mpipe_pad;

/**
 * @brief Add a pad to an element
 *
 * Adds a pad to the element. The pad's container will be set to the element.
 *
 * @param element The @ref mpipe_element to add the pad to
 * @param pad The @ref mpipe_pad to add to the element
 */
/**
 * @brief Reset every pad of an element to constraining nothing.
 *
 * Drops the negotiated capability from each of the element's pads so a
 * subsequent re-negotiation starts fresh. Base classes call this on the
 * PAUSED to READY transition; a derived element that overrides change_state
 * must chain to its base function to inherit the reset.
 *
 * @param element Pointer to the element whose pads to reset.
 */
void mpipe_element_reset_pad_caps(struct mpipe_element *element);

/**
 * @brief Attach a pad to an element.
 *
 * Adds @p pad to the element's pad list and makes the element the pad's
 * container, which is how a pad callback reaches the element that owns it.
 * A base class calls this for the pads it declares, so an element only calls
 * it for a pad of its own.
 *
 * @param element Pointer to the element to attach the pad to.
 * @param pad Pointer to the pad to attach.
 */
void mpipe_element_add_pad(struct mpipe_element *element, struct mpipe_pad *pad);

/**
 * @brief Link elements together
 *
 * Links multiple elements together in a chain. Elements should have only
 * one source and/or one sink pad. If not, the first src/sink pads will be used.
 * The function takes a variable number of elements and links them sequentially.
 *
 * @param element_1 First element in the chain
 * @param element_2 Second element in the chain
 * @param ... Additional elements to link (terminated by NULL)
 *
 * @return 0 on success, negative errno on failure
 */
int mpipe_element_link(struct mpipe_element *element_1, struct mpipe_element *element_2, ...);

/**
 * @brief Set the state of an element
 *
 * Sets the state of an element. This function will try to set the
 * requested state by going through all the intermediary states and calling
 * the element's state change function for each.
 *
 * @param element The element to change state of
 * @param state The element's new @ref mpipe_state
 * @return Result of the state change, one of @ref mpipe_state_change_return
 */
enum mpipe_state_change_return mpipe_element_set_state(struct mpipe_element *element,
						       enum mpipe_state state);

/**
 * @brief Get the bus of an element
 *
 * Retrieves the @ref mpipe_bus associated with the element, which is the bus of
 * the nearest bin that contains the element.
 *
 * @param self Pointer to the @ref mpipe_element to get the bus from.
 *
 * @return Pointer to the @ref mpipe_bus, or NULL if no bus is found.
 */
struct mpipe_bus *mpipe_element_get_bus(struct mpipe_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_MPIPE_ELEMENT_H_ */
