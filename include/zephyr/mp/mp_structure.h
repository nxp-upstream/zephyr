/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Structure header file.
 */

#ifndef ZEPHYR_INCLUDE_MP_MP_STRUCTURE_H_
#define ZEPHYR_INCLUDE_MP_MP_STRUCTURE_H_

#include <zephyr/sys/util_macro.h>
#include <zephyr/toolchain.h>

#include <zephyr/mp/mp_value.h>

/**
 * @defgroup mp_structure Capability Structure
 * @ingroup mp_framework
 * @brief Fixed-size container describing one media capability.
 *
 * An mp_structure holds a set of fields, each an @ref mp_caps_field identifier
 * paired with an @ref mp_value. It is a fixed-size type held by value, so it
 * needs no allocation: the fields occupy the first slots of two parallel
 * arrays, one naming the field and one holding its value.
 *
 * A video capability such as
 *
 *     video, format=VIDEO_PIX_FMT_RGB565, width=[16, 1280, 2], height=[16, 720, 2]
 *
 * is built directly in caller storage:
 *
 * @code{.c}
 * struct mp_structure s;
 *
 * mp_structure_init_fields(&s, MP_MEDIA_VIDEO,
 *     MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT, VIDEO_PIX_FMT_RGB565,
 *     MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT_RANGE, 16, 1280, 2,
 *     MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT_RANGE, 16, 720, 2,
 *     MP_CAPS_END);
 * @endcode
 *
 * A range is written `[min, max, step]`, so the width above admits every
 * multiple of 2 from 16 to 1280.
 *
 * Two structures intersect when they have the same media type, carry at least
 * one field identifier in common, and every field they do share has
 * intersecting values. The result is the union of both: a shared field holds
 * the intersected value, and a field only one side carries passes through
 * unchanged. That last rule is what lets a constraint travel down a chain of
 * elements that do not themselves care about it.
 *
 * Pointers passed to this API must not be NULL unless the parameter is
 * documented otherwise.
 *
 * @{
 */

/**
 * @brief Media type a capability describes
 */
enum mp_media_type {
	/** Unknown media type, what a structure constraining nothing carries */
	MP_MEDIA_UNKNOWN = 0,
	/** Audio in PCM format */
	MP_MEDIA_AUDIO_PCM,
	/** Video, including raw, bayer and compressed formats */
	MP_MEDIA_VIDEO,
	/** One past the last media type; not itself a usable media type */
	MP_MEDIA_END,
};

/**
 * @brief Caps field identifiers
 *
 * A field of an @ref mp_structure is one of these identifiers paired with an
 * @ref mp_value. Each entry below names the value type that carries it. A
 * capability offering a span rather than one setting uses the matching range
 * type instead, MP_TYPE_UINT_RANGE where the entry says MP_TYPE_UINT, which is
 * how a device advertises every width from 16 to 1280 rather than a single one.
 * Negotiation then narrows the span, and fixation picks one value out of it.
 * MP_CAPS_PIXEL_FORMAT and MP_CAPS_INTERLEAVED, for example, are the exceptions:
 * neither has a meaningful range form, so both are always carried fixed.
 *
 * A caps field is not a property. A property configures one element, and
 * nothing else has to agree on it: a camera's gain or contrast, or the device
 * an element binds to. It is set on that element and used by that element.
 *
 * A caps field describes the data crossing a link, so both ends have to agree
 * on it before anything can flow, and that agreement is what negotiation
 * settles. The test when adding one is whether two different elements would
 * have to arrive at the same value for the stream to be correct. If only the
 * element that owns it cares, it is a property.
 *
 * MP_CAPS_IMAGE_WIDTH passes that test: a camera, a converter and a display all
 * understand it and all have to agree on one number. Camera gain does not, as
 * the display neither knows nor cares what it is.
 *
 * These identifiers are shared by every domain, and one structure holds only
 * CONFIG_MP_STRUCTURE_MAX_FIELDS of them at a time, so a new field earns its
 * place by meaning something to more than the element that introduced it.
 * Prefer one a whole domain agrees on, or one that spans domains the way
 * MP_CAPS_FRAME_INTERVAL does for audio and video.
 *
 * Add it before MP_CAPS_END and document its unit and value type the way the
 * entries below do.
 */
enum mp_caps_field {
	/** Pixel format, as a VIDEO_PIX_FMT_* fourcc, MP_TYPE_UINT */
	MP_CAPS_PIXEL_FORMAT = 0,
	/** Image width in pixels, MP_TYPE_UINT */
	MP_CAPS_IMAGE_WIDTH,
	/** Image height in pixels, MP_TYPE_UINT */
	MP_CAPS_IMAGE_HEIGHT,
	/** Sampling frequency in Hz (audio), MP_TYPE_UINT */
	MP_CAPS_SAMPLE_RATE,
	/** Sample size in bits (audio), MP_TYPE_UINT */
	MP_CAPS_BITWIDTH,
	/** Number of channels (audio), MP_TYPE_UINT */
	MP_CAPS_NUM_OF_CHANNEL,
	/**
	 * Layout of the channels within a buffer (audio), MP_TYPE_BOOLEAN: true for
	 * interleaved (LRLRLRLR), false for non-interleaved (LLLLRRRR)
	 */
	MP_CAPS_INTERLEAVED,
	/**
	 * Time covered by one frame, in microseconds, MP_TYPE_UINT. Used by both the
	 * audio and the video domains: for video it is the frame interval, the
	 * reciprocal of the frame rate.
	 */
	MP_CAPS_FRAME_INTERVAL,
	/** Number of buffers to allocate in the pool, MP_TYPE_UINT */
	MP_CAPS_BUFFER_COUNT,
	/**
	 * One past the last field identifier. Bounds the range of valid
	 * identifiers, and terminates the field list of
	 * @ref mp_structure_init_fields.
	 */
	MP_CAPS_END,
};

/**
 * @brief The structure constrains nothing and intersects with anything.
 *
 * Distinct from a structure with no fields set, which constrains nothing
 * because it is empty and therefore intersects with nothing.
 */
#define MP_STRUCTURE_FLAG_ANY BIT(0)

/**
 * @struct mp_structure
 * @brief Fixed-size container holding the fields of one capability.
 *
 * Fields occupy the first `num_fields` slots of `ids` and `values`,
 * which are parallel: `ids[i]` names the field that `values[i]` holds. A slot
 * index carries no meaning of its own, so the number of identifiers that exist
 * costs nothing here; only how many fields one structure holds at once does,
 * bounded by CONFIG_MP_STRUCTURE_MAX_FIELDS.
 */
struct mp_structure {
	/** Media type of the structure, see @ref mp_media_type */
	uint8_t media_type_id;
	/** Structure flags, see MP_STRUCTURE_FLAG_ANY */
	uint8_t flags;
	/** Number of slots in use at the front of @ref ids and @ref values */
	uint8_t num_fields;
	/** Field identifier held by each slot, see @ref mp_caps_field */
	uint8_t ids[CONFIG_MP_STRUCTURE_MAX_FIELDS];
	/** Value held by each slot */
	struct mp_value values[CONFIG_MP_STRUCTURE_MAX_FIELDS];
};

/** @cond INTERNAL_HIDDEN */
#define MP_STRUCTURE_FIELD_COUNT(field_id, value) +1
#define MP_STRUCTURE_FIELD_ID(field_id, value)    (field_id),
#define MP_STRUCTURE_FIELD_VALUE(field_id, value) value,
/** @endcond */

/**
 * @brief Define a read-only @ref mp_structure in .rodata.
 *
 * An element whose capabilities are known at build time can carry them here
 * instead of building them at runtime, and copy one out with
 * @ref mp_structure_duplicate when it is enumerated.
 *
 * @p fields is a macro taking one argument, which it applies to each field as
 * `arg(field_id, value_initializer)`. Listing the fields once is what keeps the
 * identifiers, the values and the count from disagreeing, which would silently
 * drop a field. Defining more fields than CONFIG_MP_STRUCTURE_MAX_FIELDS fails
 * the build.
 *
 * @code{.c}
 * #define JPEG_SRC_FIELDS(X) X(MP_CAPS_PIXEL_FORMAT, MP_VALUE_UINT(VIDEO_PIX_FMT_JPEG))
 * MP_STRUCTURE_DEFINE(jpeg_src, MP_MEDIA_VIDEO, JPEG_SRC_FIELDS);
 * @endcode
 *
 * @param name Name of the structure to define.
 * @param media Media type of the structure, see @ref mp_media_type.
 * @param fields Macro listing the fields, see above.
 */
#define MP_STRUCTURE_DEFINE(name, media, fields)                                                   \
	BUILD_ASSERT((0 fields(MP_STRUCTURE_FIELD_COUNT)) <= CONFIG_MP_STRUCTURE_MAX_FIELDS,       \
		     #name " has more fields than CONFIG_MP_STRUCTURE_MAX_FIELDS");                \
	static const struct mp_structure name = {                                                  \
		.media_type_id = (media),                                                          \
		.flags = 0,                                                                        \
		.num_fields = 0 fields(MP_STRUCTURE_FIELD_COUNT),                                  \
		.ids = {fields(MP_STRUCTURE_FIELD_ID)},                                            \
		.values = {fields(MP_STRUCTURE_FIELD_VALUE)},                                      \
	}

/**
 * @brief Initialize an @ref mp_structure.
 *
 * Leaves the structure with no field set.
 *
 * @param structure Pointer to the structure to initialize.
 * @param media_type_id Media type of the structure, see @ref mp_media_type.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p structure is NULL or @p media_type_id is not a media type
 */
int mp_structure_init(struct mp_structure *structure, uint8_t media_type_id);

/**
 * @brief Initialize an @ref mp_structure with a media type and a list of fields.
 *
 * Builds a capability directly into caller storage: repeated
 * `field_id, type, value-args...` triples terminated by MP_CAPS_END, with the
 * same per-type argument rules as @ref mp_value_set. Allocates nothing, so a
 * capability can be described on the stack or in a caller's own slot.
 *
 * @param structure Pointer to the structure to initialize.
 * @param media_type_id Media type of the structure, see @ref mp_media_type.
 * @param ... Field triples, terminated by MP_CAPS_END.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p structure is NULL, @p media_type_id is not a media
 *         type, or a field identifier or value type is invalid
 * @retval -EEXIST if the list names the same field twice
 * @retval -ENOSPC if the list holds more fields than
 *         CONFIG_MP_STRUCTURE_MAX_FIELDS
 */
int mp_structure_init_fields(struct mp_structure *structure, uint8_t media_type_id, ...);

/**
 * @brief Initialize a structure that constrains nothing.
 *
 * Intersecting it with another structure yields a copy of that structure, and
 * fixating it reports -ENOENT because there is nothing to choose.
 *
 * @param structure Pointer to the structure to initialize.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p structure is NULL
 */
int mp_structure_init_any(struct mp_structure *structure);

/**
 * @brief Check whether a structure constrains nothing.
 *
 * @param structure Pointer to the structure to check, may be NULL.
 *
 * @return true if the structure constrains nothing, false otherwise or if @p
 *         structure is NULL
 */
bool mp_structure_is_any(const struct mp_structure *structure);

/**
 * @brief Check whether an @ref mp_structure matches nothing.
 *
 * The counterpart of @ref mp_structure_is_any: a structure that constrains
 * nothing because it holds no field intersects with nothing, where an ANY one
 * intersects with everything.
 *
 * @param structure Pointer to the structure to check, may be NULL.
 *
 * @return true if the structure is empty, false otherwise or if @p structure
 *         is NULL
 */
bool mp_structure_is_empty(const struct mp_structure *structure);

/**
 * @brief Append a field to an @ref mp_structure from caller-owned storage.
 *
 * Copies @p value into the next free slot; the caller keeps its own copy.
 *
 * @param structure Pointer to the structure to append the field to.
 * @param field_id Field identifier, see @ref mp_caps_field. Must not already be set.
 * @param value Pointer to the field value to copy in.
 *
 * @retval 0 on success
 * @retval -EINVAL if a pointer is NULL or @p field_id is not a field identifier
 * @retval -EEXIST if the structure already carries @p field_id
 * @retval -ENOSPC if the structure already holds
 *         CONFIG_MP_STRUCTURE_MAX_FIELDS fields
 */
int mp_structure_append_value(struct mp_structure *structure, uint8_t field_id,
			      const struct mp_value *value);

/**
 * @brief Copy a field from one @ref mp_structure to another.
 *
 * Leaves @p dst untouched and reports -ENOENT when @p src does not carry the
 * field, so a caller building a capability out of another one can offer every
 * field it wants to pass through and ignore that code, rather than testing for
 * each of them first.
 *
 * @param src Pointer to the structure to read the field from.
 * @param dst Pointer to the structure to append the field to.
 * @param field_id Field identifier, see @ref mp_caps_field.
 *
 * @retval 0 on success
 * @retval -ENOENT if @p src does not carry the field
 * @retval -EINVAL if @p dst is NULL or @p field_id is not a field identifier
 * @retval -EEXIST if @p dst already carries the field
 * @retval -ENOSPC if @p dst is full
 */
int mp_structure_copy_field(const struct mp_structure *src, struct mp_structure *dst,
			    uint8_t field_id);

/**
 * @brief Empty an @ref mp_structure, dropping every field it carries.
 *
 * The structure keeps its media type and its flags, so it can be filled in
 * again without being initialized first.
 *
 * This leaves it constraining nothing because it holds no field, which
 * intersects with nothing. It does not make it an ANY structure, which
 * constrains nothing but intersects with everything: use
 * @ref mp_structure_init_any for that.
 *
 * @param structure Pointer to the structure to clear.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p structure is NULL
 */
int mp_structure_clear(struct mp_structure *structure);

/**
 * @brief Check if an @ref mp_structure is fixed.
 *
 * A structure is fixed when it carries at least one field and every field it
 * carries holds a single value rather than a range. A structure that
 * constrains nothing is never fixed, whether it is flagged ANY or simply
 * holds no field: there is nothing to have settled.
 *
 * @param structure Pointer to the structure to check, may be NULL.
 *
 * @return true if the structure is fixed, false otherwise or if @p structure
 *         is NULL
 */
bool mp_structure_is_fixed(const struct mp_structure *structure);

/**
 * @brief Get the value of a field in an @ref mp_structure.
 *
 * Retrieves the value associated with the specified field ID.
 *
 * @param structure Pointer to the structure containing the field, may be NULL.
 * @param field_id Field identifier, see @ref mp_caps_field.
 *
 * @return Pointer to the value of the field, NULL if the structure does not
 *         carry it or @p structure is NULL.
 */
const struct mp_value *mp_structure_get_value(const struct mp_structure *structure,
					      uint8_t field_id);

/**
 * @brief Remove a field from an @ref mp_structure.
 *
 * Deletes the field with the specified ID from the structure.
 *
 * @param structure Pointer to the structure containing the field to remove.
 * @param field_id Field identifier, see @ref mp_caps_field.
 *
 * @retval 0 on success
 * @retval -EINVAL if @p structure is NULL
 * @retval -ENOENT if the structure does not carry the field
 */
int mp_structure_remove_field(struct mp_structure *structure, uint8_t field_id);

/**
 * @brief Check if two @ref mp_structure can intersect.
 *
 * Two structures can intersect when they share the same media type, carry at
 * least one field identifier in common, and every field they do share has
 * intersecting values. A structure that constrains nothing intersects with
 * anything, so an ANY structure always can.
 *
 * @param struct1 Pointer to the first structure, may be NULL.
 * @param struct2 Pointer to the second structure, may be NULL.
 *
 * @return true if the structures can intersect, false otherwise or if either
 *         pointer is NULL
 */
bool mp_structure_can_intersect(const struct mp_structure *struct1,
				const struct mp_structure *struct2);

/**
 * @brief Intersect two structures into caller-provided storage.
 *
 * Requires the same conditions as @ref mp_structure_can_intersect. The result
 * is the union of both inputs: a field both sides carry holds the intersected
 * value, and a field only one side carries is copied through unchanged, which
 * can leave the result holding more fields than either input. Intersecting an
 * ANY structure yields a copy of the other one.
 *
 * @param struct1 Pointer to the first structure.
 * @param struct2 Pointer to the second structure.
 * @param out Pointer to storage for the result, left untouched on -EINVAL
 *            and cleared on any other failure.
 *
 * @retval 0 on success
 * @retval -ENOENT if the structures share no field, or a shared field has no
 *         common value
 * @retval -EINVAL if a pointer is NULL or the media types differ
 * @retval -ENOSPC if the union does not fit CONFIG_MP_STRUCTURE_MAX_FIELDS
 */
int mp_structure_intersect(const struct mp_structure *struct1, const struct mp_structure *struct2,
			   struct mp_structure *out);

/**
 * @brief Fixate a structure into caller-provided storage.
 *
 * Each field is reduced to a single value: a range to its minimum, a value
 * already fixed to itself.
 *
 * @param src Pointer to the structure to fixate.
 * @param out Pointer to storage for the result, left untouched on -EINVAL
 *            and cleared on any other failure.
 *
 * @retval 0 on success
 * @retval -ENOENT if @p src constrains nothing and so has nothing to fixate,
 *         whether it is flagged ANY or simply holds no field
 * @retval -EINVAL if either pointer is NULL
 */
int mp_structure_fixate(const struct mp_structure *src, struct mp_structure *out);

/**
 * @brief Copy a structure into caller-provided storage.
 *
 * Unlike the other operations that write into caller storage, this one carries
 * the flags over too, so duplicating an ANY structure yields an ANY structure.
 *
 * @param src Pointer to the structure to copy.
 * @param out Pointer to storage for the copy.
 *
 * @return 0 on success, -EINVAL if either pointer is NULL
 */
int mp_structure_duplicate(const struct mp_structure *src, struct mp_structure *out);

/**
 * @brief Print an @ref mp_structure.
 *
 * Outputs the contents of the structure for debugging or inspection.
 *
 * @param structure Pointer to the structure to print.
 */
void mp_structure_print(const struct mp_structure *structure);

/** @} */

#endif /*ZEPHYR_INCLUDE_MP_MP_STRUCTURE_H_*/
