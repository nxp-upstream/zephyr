/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>

#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/vid/mp_vid_property.h>
#include <zephyr/mp/vid/mp_vid_transform.h>

LOG_MODULE_REGISTER(mp_vid_transform, CONFIG_MP_LOG_LEVEL);

#define DEFAULT_PROP_DEVICE DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_videotrans))

static int mp_vid_transform_chainfn(struct mp_pad *pad, struct net_buf *in_buf,
				    struct net_buf **out_buf)
{
	int ret;
	struct mp_transform *transform =
		CONTAINER_OF(pad->object.container, struct mp_transform, element.object);
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)transform;
	struct mp_buffer_pool *outpool = &vid_transform->vid_obj_out.pool.pool;
	struct video_buffer *in_vbuf;

	/* TODO: Ensure net_buf meta's driver_buf is always a video buffer */
	if (mp_buffer_get_meta(in_buf)->driver_buf == NULL) {
		in_vbuf = video_import_buffer(in_buf->data, in_buf->size);
	} else {
		in_vbuf = mp_buffer_get_meta(in_buf)->driver_buf;
	}
	in_vbuf->bytesused = mp_buffer_get_meta(in_buf)->bytes_used;

	/* Enqueue input buffer */
	in_vbuf->type = VIDEO_BUF_TYPE_INPUT;
	if (video_enqueue(vid_transform->vid_obj_in.vdev, in_vbuf) != 0) {
		LOG_ERR("Failed to enqueue input buffer");
		net_buf_unref(in_buf);
		return -EIO;
	}

	/* Dequeue an input buffer, blocking */
	struct video_buffer *vbuf = &(struct video_buffer){.type = vid_transform->vid_obj_in.type};

	ret = video_dequeue(vid_transform->vid_obj_in.vdev, &vbuf, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to dequeue input buffer");
		net_buf_unref(in_buf);
		return -EIO;
	}

	/* Done with the input buffer */
	net_buf_unref(in_buf);

	/* Dequeue an output buffer, blocking */
	ret = outpool->acquire_buffer(outpool, out_buf);
	if (ret != 0) {
		LOG_ERR("Failed to acquire output buffer");
		return -ENOMEM;
	}

	return 0;
}

static int mp_vid_transform_enum_caps(struct mp_pad *pad, uint32_t index,
				      const struct mp_structure *filter, struct mp_structure *out)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)pad->object.container;
	struct mp_vid_object *vid_obj;

	if (pad->direction == MP_PAD_SINK) {
		vid_obj = &vid_transform->vid_obj_in;
	} else if (pad->direction == MP_PAD_SRC) {
		vid_obj = &vid_transform->vid_obj_out;
	} else {
		return -EINVAL;
	}

	return mp_vid_object_enum_caps(vid_obj, index, filter, out);
}

static int mp_vid_transform_set_caps(struct mp_transform *transform,
				     enum mp_pad_direction direction,
				     const struct mp_structure *caps)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)transform;
	struct mp_vid_object *vid_obj = NULL;

	if (direction == MP_PAD_SINK) {
		vid_obj = &vid_transform->vid_obj_in;
	}

	if (direction == MP_PAD_SRC) {
		vid_obj = &vid_transform->vid_obj_out;
	}

	if (vid_obj == NULL || mp_vid_object_set_caps(vid_obj, caps) < 0) {
		return -EINVAL;
	}

	/* Set pad's caps only when everything is OK */
	return mp_pad_set_caps(direction == MP_PAD_SRC ? &transform->srcpad : &transform->sinkpad,
			       caps);
}

static int mp_vid_transform_transform_caps(struct mp_transform *self,
					   enum mp_pad_direction direction,
					   const struct mp_structure *in, uint32_t index,
					   struct mp_structure *out)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)self;
	const struct device *dev = vid_transform->vid_obj_in.vdev;
	struct video_format_cap vfc, other_vfc;

	if (direction != MP_PAD_SINK && direction != MP_PAD_SRC) {
		return -EINVAL;
	}

	if (in == NULL || mp_vid_caps_to_vfc(in, &vfc) < 0) {
		return -ENOENT;
	}

	if (video_transform_cap(dev, &vfc, &other_vfc, direction, (uint16_t)index) != 0) {
		return -ENOENT;
	}

	return mp_vid_vfc_to_caps(&other_vfc, out);
}

static int mp_vid_transform_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)obj;

	switch (key) {
	case MP_PROP_VID_DEVICE: {
		const struct device *prev_vdev = vid_transform->vid_obj_in.vdev;

		mp_vid_object_set_property(&vid_transform->vid_obj_in, key, val);
		mp_vid_object_set_property(&vid_transform->vid_obj_out, key, val);

		/*
		 * Re-read the pool parameters from the new device. Probing disturbs
		 * the compose selection, so skip it when nothing changed.
		 */
		if (vid_transform->vid_obj_in.vdev != prev_vdev) {
			(void)mp_vid_object_probe_bounds(&vid_transform->vid_obj_in);
			(void)mp_vid_object_probe_bounds(&vid_transform->vid_obj_out);
		}

		return 0;
	}
	default:
		return mp_vid_object_set_property(&vid_transform->vid_obj_in, key, val);
	}
}

static int mp_vid_transform_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)obj;

	return mp_vid_object_get_property(&vid_transform->vid_obj_in, key, val);
}

static int mp_vid_transform_decide_allocation(struct mp_transform *self, struct mp_dispatch *query)
{
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)self;

	return mp_vid_object_decide_allocation(&vid_transform->vid_obj_out, query);
}

static int mp_vid_transform_propose_allocation(struct mp_transform *self, struct mp_dispatch *query)
{
	return mp_dispatch_set_pool(query, self->inpool);
}

void mp_vid_transform_init(struct mp_element *self)
{
	struct mp_transform *transform = (struct mp_transform *)self;
	struct mp_vid_transform *vid_transform = (struct mp_vid_transform *)transform;

	/* Init base class */
	mp_transform_init(self);

	/* Initialize vid objects */
	vid_transform->vid_obj_in.vdev = DEFAULT_PROP_DEVICE;
	vid_transform->vid_obj_out.vdev = DEFAULT_PROP_DEVICE;
	vid_transform->vid_obj_in.type = VIDEO_BUF_TYPE_INPUT;
	vid_transform->vid_obj_out.type = VIDEO_BUF_TYPE_OUTPUT;

	self->object.set_property = mp_vid_transform_set_property;
	self->object.get_property = mp_vid_transform_get_property;

	/*
	 * m2m devices have both input and output buffer queues,
	 * so it should be in normal mode by default
	 */
	transform->mode = MP_MODE_NORMAL;

	transform->inpool = &vid_transform->vid_obj_in.pool.pool;
	transform->outpool = &vid_transform->vid_obj_out.pool.pool;
	/* Initialize buffer pools */
	mp_vid_buffer_pool_init(transform->inpool, &(vid_transform->vid_obj_in));
	mp_vid_buffer_pool_init(transform->outpool, &(vid_transform->vid_obj_out));

	/*
	 * Probe the pool parameters for both directions here. The formats are
	 * enumerated from the device on demand instead, so the pad caps stay ANY
	 * until one is negotiated. The pools cannot wait for that enumeration:
	 * the src pad's capability comes out of transform_caps(), so vid_obj_out
	 * is never enumerated.
	 */
	(void)mp_vid_object_probe_bounds(&vid_transform->vid_obj_in);
	(void)mp_vid_object_probe_bounds(&vid_transform->vid_obj_out);

	transform->sinkpad.enum_capsfn = mp_vid_transform_enum_caps;
	transform->srcpad.enum_capsfn = mp_vid_transform_enum_caps;

	transform->set_caps = mp_vid_transform_set_caps;
	transform->transform_caps = mp_vid_transform_transform_caps;
	transform->sinkpad.chainfn = mp_vid_transform_chainfn;
	transform->decide_allocation = mp_vid_transform_decide_allocation;
	transform->propose_allocation = mp_vid_transform_propose_allocation;
}
