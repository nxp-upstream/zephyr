/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_pad.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_PAD_H_
#define ZEPHYR_INCLUDE_MP_MP_PAD_H_

/**
 * @defgroup mp_pad Pad
 * @ingroup mp_framework
 * @brief Connection point for data flow between elements
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/kernel.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_object.h>

struct mp_dispatch;

/**
 * @brief The direction of a pad
 */
enum mp_pad_direction {
	/** Direction is unknown */
	MP_PAD_UNKNOWN,
	/** The pad is a source pad */
	MP_PAD_SRC,
	/** The pad is a sink pad */
	MP_PAD_SINK
};

/**
 * @brief The operating mode of a @ref mp_pad
 *
 * Defines if the pad operates in push or pull mode or none of them.
 */
enum mp_pad_mode {
	/** Pad will not handle dataflow */
	MP_PAD_MODE_NONE,
	/** Pad handles dataflow in push mode */
	MP_PAD_MODE_PUSH,
	/** Pad handles dataflow in pull mode */
	MP_PAD_MODE_PULL
};

/**
 * @brief The presence of a pad
 */
enum mp_pad_presence {
	/** The pad is always present */
	MP_PAD_ALWAYS,
	/** The pad will present depending on the media stream */
	MP_PAD_SOMETIMES,
	/** The pad is only available on request */
	MP_PAD_REQUEST
};

/**
 * @brief Pad flags
 */
enum mp_pad_flags {
	/** Pad needs to pass through the negotiation process */
	MP_PAD_FLAG_NEGOTIATE = BIT(0),
};

/**
 * @brief Pad structure
 *
 * The pad structure represents a connection point of an element.
 * Pads are used to negotiate capabilities and transfer data between elements.
 */
struct mp_pad {
	/** Base object */
	struct mp_object object;
	/** The direction of the pad, cannot change after creating the pad */
	enum mp_pad_direction direction;
	/** The presence of the pad */
	enum mp_pad_presence presence;
	/** The operating mode of the pad */
	enum mp_pad_mode mode;
	/** Pointer to the peer pad this pad is linked to */
	struct mp_pad *peer;
	/**
	 * The pad's capability. ANY until one is negotiated, and reset back to
	 * ANY on PAUSED to READY. Held by value, so a pad owns its capability
	 * outright and nothing has to be allocated to describe one.
	 */
	struct mp_structure caps;
	/** Flushing gate. While set, buffers are dropped instead of chained */
	atomic_t flushing;

	/** Chain function for handling buffers */
	int (*chainfn)(struct mp_pad *pad, struct net_buf *in_buf, struct net_buf **out_buf);
	/** Query function for handling queries */
	int (*queryfn)(struct mp_pad *pad, struct mp_dispatch *query);
	/** Event function for handling events */
	int (*eventfn)(struct mp_pad *pad, struct mp_dispatch *event);
	/**
	 * Enumerate the pad's supported caps one structure at a time into caller storage.
	 *
	 * Lets an element whose capabilities come from a device produce them on
	 * demand instead of holding a structure for each. Defaults to producing
	 * the pad's own capability, and nothing past index 0.
	 */
	int (*enum_capsfn)(struct mp_pad *pad, uint32_t index, const struct mp_structure *filter,
			   struct mp_structure *out);
};

/**
 * @brief Produce one of the pad's supported caps.
 *
 * @param pad Pad to enumerate.
 * @param index Zero-based index of the capability.
 * @param filter Optional structure to narrow the capability by, may be NULL.
 * @param out Storage for the capability, released with @ref mp_structure_clear.
 *
 * @return 0 on success, -EAGAIN if this index cannot satisfy @p filter,
 *         -ENOENT past the last capability
 */
int mp_pad_enum_caps(struct mp_pad *pad, uint32_t index, const struct mp_structure *filter,
		     struct mp_structure *out);

/**
 * @brief Produce the pad's first supported capability that a filter accepts.
 *
 * @param pad Pad to enumerate.
 * @param filter Capability to narrow by, may be NULL or ANY.
 * @param out Storage for the capability, released with @ref mp_structure_clear.
 *
 * @return 0 on success, -ENODATA if no capability is accepted
 */
int mp_pad_enum_first(struct mp_pad *pad, const struct mp_structure *filter,
		      struct mp_structure *out);

/**
 * @brief Initialize a pad
 *
 * Initializes an existing @ref mp_pad structure with the specified parameters.
 *
 * @param pad Pointer to the @ref mp_pad to initialize
 * @param id Unique ID of the pad instance in the element
 * @param direction Direction of the pad (@ref mp_pad_direction)
 * @param presence Presence of the pad (@ref mp_pad_presence)
 */
void mp_pad_init(struct mp_pad *pad, uint8_t id, enum mp_pad_direction direction,
		 enum mp_pad_presence presence);

/**
 * @brief Set the pad's capability.
 *
 * Copies @p caps into the pad. Passing NULL resets the pad to constraining
 * nothing, which is what a re-negotiation starts from.
 *
 * @param pad Pad to set the capability on.
 * @param caps Capability to copy in, or NULL to reset to ANY.
 *
 * @return 0 on success, negative errno on failure
 */
int mp_pad_set_caps(struct mp_pad *pad, const struct mp_structure *caps);

/**
 * @brief Link two pads together
 *
 * Links a source pad to a sink pad, establishing a connection for data flow.
 * Both pads will have their peer pointers set to each other.
 *
 * @param srcpad Source pad to link
 * @param sinkpad Sink pad to link
 *
 * @return 0 on success, negative errno on failure
 */
int mp_pad_link(struct mp_pad *srcpad, struct mp_pad *sinkpad);

/**
 * @brief Send an event to a pad
 *
 * Sends an event to the specified pad using the pad's event function.
 *
 * @param pad Pointer to the @ref mp_pad where the event should be sent
 * @param event Pointer to the @ref mp_dispatch to send
 *
 * @return 0 on success, negative errno on failure
 */
int mp_pad_send_event(struct mp_pad *pad, struct mp_dispatch *event);

/**
 * @brief Default event handler for pads
 *
 * Forwards an event received on @p pad to the peers of all opposite-side pads
 * in the same element. If @p pad is a sink pad, the event is forwarded to the
 * peer of each source pad; if @p pad is a source pad, it is forwarded to the
 * peer of each sink pad.
 *
 * If the element has only source pads or only sink pads, there are no
 * opposite-side pads to send the event to, so the function returns
 * -ENOTSUP.
 *
 * @param pad Pointer to the @ref mp_pad that received the event
 * @param event Pointer to the @ref mp_dispatch to send
 *
 * @return 0 if the event was successfully forwarded to at least one peer,
 *         -ENOTSUP if there are no opposite-side pads or none have a peer,
 *         negative errno on other failures
 */
int mp_pad_send_event_default(struct mp_pad *pad, struct mp_dispatch *event);

/**
 * @brief Send a query to a pad
 *
 * Sends a query to the pad using the pad's query function.
 *
 * @param pad Pointer to the @ref mp_pad to send query to
 * @param query Pointer to the @ref mp_dispatch to send
 *
 * @return 0 on success, negative errno on failure
 */
int mp_pad_query(struct mp_pad *pad, struct mp_dispatch *query);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_PAD_H_ */
