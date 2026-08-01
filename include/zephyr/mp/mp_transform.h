/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Main header for mp_transform.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_TRANSFORM_H_
#define ZEPHYR_INCLUDE_MP_MP_TRANSFORM_H_

/**
 * @defgroup mp_transform Transforms
 * @ingroup mp_framework
 * @brief Elements that process data between sink and source pads.
 *
 * @{
 */

#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_pad.h>
#include <zephyr/mp/mp_structure.h>

/**
 * @brief Properties for a transform element
 *
 * Enumeration of properties that can be configured for base transform
 */
enum mp_prop_transform {
	/** Last transform property marker (for validation/iteration) */
	MP_PROP_TRANSFORM_LAST,
};

/**
 * @brief Operating modes of a transform element
 *
 * The modes specifies how input and output buffers are handled.
 */
enum mp_transform_mode {
	/** The buffer is kept intact. */
	MP_MODE_PASSTHROUGH = 0,
	/** The input buffer is directly modified. Input and output buffers are the same. */
	MP_MODE_INPLACE = 1,
	/** The output buffer is allocated and differs from the input buffer. */
	MP_MODE_NORMAL = 2,
};

/**
 * @brief Transform element structure
 *
 * Base structure for all transform elements in the media pipeline.
 * Transform elements process data from their sink pad and output
 * the result to their source pad.
 */
struct mp_transform {
	/** Base element structure */
	struct mp_element element;
	/** Sink pad for receiving input data */
	struct mp_pad sinkpad;
	/** Source pad for sending output data */
	struct mp_pad srcpad;
	/** Pointer to the input buffer pool for allocating input buffers */
	struct mp_buffer_pool *inpool;
	/** Pointer to the output buffer pool for allocating output buffers */
	struct mp_buffer_pool *outpool;
	/** Operating mode determining buffer handling strategy */
	enum mp_transform_mode mode;

	/**
	 * @brief Set a given caps to an element's pad
	 * @param transform Pointer to the transform element
	 * @param direction Direction of the pad (@ref mp_pad_direction)
	 * @param caps Capability to set (@ref mp_structure)
	 * @return 0 on success, negative errno on failure
	 */
	int (*set_caps)(struct mp_transform *transform, enum mp_pad_direction direction,
			const struct mp_structure *caps);
	/**
	 * @brief Produce one transformation of a capability across the element
	 *
	 * An element commonly maps a single input capability to several output
	 * ones: a decoder that can emit any of N pixel formats, a converter with
	 * N reachable formats. Those alternatives live on @p index rather than in
	 * a returned set, so a negotiation walks them one at a time and the fan
	 * out is never materialized.
	 *
	 * @param self Pointer to the transform element
	 * @param direction Direction of the pad to transform the capability to
	 *                  (@ref mp_pad_direction)
	 * @param in Capability on the opposite pad to transform
	 * @param index Index of the transformation to produce, starting at 0
	 * @param out Caller storage receiving the transformation
	 *
	 * @return 0 when @p out holds a transformation, -EAGAIN when this index
	 *         produces none but a later one may, -ENOENT past the last one,
	 *         or another negative errno on failure
	 */
	int (*transform_caps)(struct mp_transform *self, enum mp_pad_direction direction,
			      const struct mp_structure *in, uint32_t index,
			      struct mp_structure *out);

	/**
	 * @brief Propose allocation parameters to upstream
	 *
	 * The transform element may propose its input buffer pool to its upstream peer.
	 * All the proposed pool's ops are then intended to be called by the upstream element.
	 *
	 * For in-place transform, the same pool may be used for both input and output. If the
	 * pool is proposed to upstream, the element has to use @ref mp_transform::inpool to
	 * point to the pool and leave @ref mp_transform::outpool as NULL so that the pool is
	 * configured / started only by the upstream and not by the transform element itself.
	 *
	 * @param self Pointer to the transform element
	 * @param query Allocation query (@ref mp_dispatch)
	 * @return 0 on success, negative errno on failure
	 */
	int (*propose_allocation)(struct mp_transform *self, struct mp_dispatch *query);
	/**
	 * @brief Decide allocation parameters for downstream
	 * @param self Pointer to the transform element
	 * @param query Allocation query (@ref mp_dispatch)
	 * @return 0 on success, negative errno on failure
	 */
	int (*decide_allocation)(struct mp_transform *self, struct mp_dispatch *query);
};

/**
 * @brief Initialize a transform element
 *
 * This function initializes the base transform element structure,
 * sets up sink and source pads, and configures default function
 * pointers for element operations.
 *
 * @param self Pointer to the element to initialize (@ref mp_element)
 */
void mp_transform_init(struct mp_element *self);

/**
 * @brief Set capabilities on a transform element's pad.
 *
 * @param transform Pointer to the transform element.
 * @param direction Direction of the pad to set caps on (@ref mp_pad_direction).
 * @param caps Pointer to the capabilities to set.
 *
 * @return 0 on success, negative errno on failure
 */
int mp_transform_set_caps(struct mp_transform *transform, enum mp_pad_direction direction,
			  const struct mp_structure *caps);

/**
 * @brief Change state function for the base transform element
 *
 * On the PAUSED to READY transition this resets the negotiated pad caps back to
 * ANY so a subsequent re-negotiation starts fresh.
 * Derived transforms that override change_state must chain to this base
 * function to inherit that behavior.
 *
 * @param self Pointer to the @ref mp_element
 * @param transition Transition state, see @ref mp_state_change
 *
 * @return One of @ref mp_state_change_return
 */
enum mp_state_change_return mp_transform_change_state(struct mp_element *self,
						      enum mp_state_change transition);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_MP_TRANSFORM_H_ */
