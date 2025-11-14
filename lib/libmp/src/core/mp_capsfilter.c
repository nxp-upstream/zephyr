/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mp_capsfilter.h"

int mp_caps_filter_set_property(MpObject *obj, uint32_t key, const void *val)
{
	MpTransform *transform = MP_TRANSFORM(obj);

	switch (key) {
	case PROP_CAPS:
		mp_caps_replace(&transform->sinkpad.caps, (MpCaps *)val);
		mp_caps_replace(&transform->srcpad.caps, (MpCaps *)val);
		return 0;
	default:
		return mp_transform_set_property(obj, key, val);
	}
}

int mp_caps_filter_get_property(MpObject *obj, uint32_t key, void *val)
{
	MpTransform *transform = MP_TRANSFORM(obj);

	switch (key) {
	case PROP_CAPS:
		/*
		 * The pad's caps may change during and after caps negotiation but the function is
		 * generally called before any pipeline process, so it's OK to get the filter caps
		 * from the pad's caps
		 */
		val = transform->sinkpad.caps;
		return 0;
	default:
		return mp_transform_get_property(obj, key, val);
	}
}

void mp_caps_filter_init(MpElement *self)
{
	MpTransform *transform = MP_TRANSFORM(self);

	/* Init base class */
	mp_transform_init(self);

	self->object.set_property = mp_caps_filter_set_property;
	self->object.get_property = mp_caps_filter_get_property;

	transform->mode = MP_MODE_PASSTHROUGH;
	/* All-pass filter is set by default */
	transform->sinkpad.caps = mp_caps_new_any();
	transform->srcpad.caps = mp_caps_new_any();
}
