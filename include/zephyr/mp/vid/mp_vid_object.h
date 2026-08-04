/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video object shared by vid source and transform elements.
 *
 * Provides common video device interaction (capabilities, format negotiation,
 * buffer pool management) used by @ref mp_vid_src and @ref mp_vid_transform.
 */

#ifndef ZEPHYR_INCLUDE_MP_VID_MP_VID_OBJECT_H_
#define ZEPHYR_INCLUDE_MP_VID_MP_VID_OBJECT_H_

/**
 * @defgroup mp_vid_objects Objects
 * @ingroup mp_vid
 * @brief Shared video device objects.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/video.h>

#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/vid/mp_vid_buffer_pool.h>

struct mp_dispatch;

/**
 * @brief Video object structure.
 *
 * Wraps a Zephyr video device with its associated buffer pool and
 * crop region, providing helpers for capability and format negotiation.
 */
struct mp_vid_object {
	/** Pointer to the video device */
	const struct device *vdev;
	/** Video buffer type (input or output) */
	enum video_buf_type type;
	/** Associated buffer pool */
	struct mp_vid_buffer_pool pool;
	/** Rectangle area to crop the input image in pixels */
	struct video_rect crop;
	/** @cond INTERNAL_HIDDEN */
	/**
	 * Device bounds shared by every enumerated capability. Probing them
	 * disturbs the device's compose selection, so they are refreshed once
	 * when the enumeration restarts rather than for each capability.
	 */
	struct {
		struct video_caps vcaps;
		uint32_t crop_w;
		uint32_t crop_h;
		uint32_t comp_min_w;
		uint32_t comp_min_h;
		uint32_t comp_max_w;
		uint32_t comp_max_h;
	} bounds;
	/** @endcond */
};

/**
 * @brief Enumerate the capabilities of the video device one at a time.
 *
 * Builds the capability of the device's format entry at @p index directly into
 * @p out, so negotiation never has to hold a structure for every format the
 * device supports.
 *
 * The device bounds are re-probed when @p index is 0, which is where an
 * enumeration starts, so a full pass costs exactly one probe.
 *
 * @param vid_obj Video object to enumerate.
 * @param index Zero-based index of the device format entry.
 * @param filter Optional structure to narrow the capability by, may be NULL.
 * @param out Storage for the capability, released with @ref mp_structure_clear.
 *
 * @return 0 on success, -EAGAIN if this index cannot satisfy @p filter,
 *         -ENOENT past the last format entry, negative errno on failure
 */
int mp_vid_object_enum_caps(struct mp_vid_object *vid_obj, uint32_t index,
			    const struct mp_structure *filter, struct mp_structure *out);

/**
 * @brief Probe the device bounds and the pool parameters they carry.
 *
 * Reads the device's capabilities and selection bounds, and takes the buffer
 * pool's minimum buffer count and alignment from them. An enumeration does this
 * on its own at index 0, so an element only has to call it for a video object
 * whose pad is not enumerated, and again whenever its device changes.
 *
 * @param vid_obj Video object to probe.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vid_object_probe_bounds(struct mp_vid_object *vid_obj);

/**
 * @brief Convert an @ref mp_structure to a @ref video_format_cap.
 *
 * @param structure Pointer to the @ref mp_structure containing video format information.
 * @param vfc       Pointer to @ref video_format_cap to populate.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_structure_to_vfc(const struct mp_structure *structure, struct video_format_cap *vfc);

/**
 * @brief Convert a fixed @ref mp_structure to a @ref video_format.
 *
 * A negotiated capability is fixed, so it holds one width and one height and
 * the format takes them as they are. Only what the capability describes is
 * written: the pitch and the size are the device's to fill in when the format
 * is applied.
 *
 * @param structure Pointer to the fixed capability to convert.
 * @param type      Buffer type of the device the format is meant for.
 * @param fmt       Pointer to @ref video_format to populate.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_structure_to_format(const struct mp_structure *structure, enum video_buf_type type,
			   struct video_format *fmt);

/**
 * @brief Convert a @ref video_format_cap to an @ref mp_structure.
 *
 * Writes into caller storage and allocates nothing, so a capability a device
 * reports can be described on the stack.
 *
 * @param vfc Pointer to the @ref video_format_cap to describe.
 * @param out Pointer to the @ref mp_structure to populate.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vfc_to_structure(const struct video_format_cap *vfc, struct mp_structure *out);

/**
 * @brief Set a property on the video object.
 *
 * @param vid_obj Pointer to the @ref mp_vid_object.
 * @param key      Property key / control ID.
 * @param val      Property value to set.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vid_object_set_property(struct mp_vid_object *vid_obj, uint32_t key, const void *val);

/**
 * @brief Get a property from the video object.
 *
 * @param vid_obj Pointer to the @ref mp_vid_object.
 * @param key      Property key / control ID to retrieve.
 * @param val      Pointer to store the retrieved property value.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vid_object_get_property(struct mp_vid_object *vid_obj, uint32_t key, void *val);

/**
 * @brief Set capabilities on the video object.
 *
 * Configures the video device with the specified fixed capabilities
 * (video format and frame rate).
 *
 * @param vid_obj Pointer to the @ref mp_vid_object.
 * @param caps     Pointer to the fixed @ref mp_structure to set.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vid_object_set_caps(struct mp_vid_object *vid_obj, const struct mp_structure *caps);

/**
 * @brief Decide buffer allocation parameters.
 *
 * Negotiates buffer allocation between the video object's own pool and
 * the requirements carried by @p query. The video object always uses
 * its own pool and only negotiates configuration parameters.
 *
 * @param vid_obj Pointer to the @ref mp_vid_object.
 * @param query    Pointer to @ref mp_dispatch containing allocation requirements.
 *
 * @return 0 on success or a negative errno code on failure.
 */
int mp_vid_object_decide_allocation(struct mp_vid_object *vid_obj, struct mp_dispatch *query);

/** @} */

#endif /* ZEPHYR_INCLUDE_MP_VID_MP_VID_OBJECT_H_ */
