/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/vid/mpipe_vid_buffer_pool_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_object.h>
#include <zephyr/mpipe/vid/mpipe_vid_sink_client.h>

LOG_MODULE_REGISTER(mpipe_vid_sink_client, CONFIG_MPIPE_LOG_LEVEL);

static int mpipe_vid_sink_client_enum_caps(struct mpipe_pad *pad, uint32_t index,
					   const struct mpipe_structure *filter,
					   struct mpipe_structure *out)
{
	struct mpipe_vid_sink_client *vsc = (struct mpipe_vid_sink_client *)pad->object.container;
	struct mpipe_structure candidate;
	struct video_format_cap vfc;
	int ret;

	if (index > UINT8_MAX || vsc->format->get_format_caps(vsc->sink.channel, MPIPE_PAD_SINK,
							      (uint8_t)index, &vfc) != 0) {
		return -ENOENT;
	}

	ret = mpipe_vid_vfc_to_caps(&vfc, &candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_vid_sink_client_set_caps(struct mpipe_sink *sink,
					  const struct mpipe_structure *caps)
{
	struct mpipe_vid_sink_client *vsc = (struct mpipe_vid_sink_client *)sink;
	struct video_format fmt;
	int ret;

	if (caps == NULL || !mpipe_structure_is_fixed(caps)) {
		return -EINVAL;
	}

	ret = mpipe_vid_caps_to_format(caps, VIDEO_BUF_TYPE_INPUT, &fmt);
	if (ret < 0) {
		return ret;
	}

	ret = vsc->format->set_format(vsc->sink.channel, &fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format: pixfmt=%u w=%u h=%u", fmt.pixelformat, fmt.width,
			fmt.height);
		return ret;
	}

	vsc->in_pool.pool.config.size = fmt.size;

	return mpipe_pad_set_caps(&sink->sink_pad, caps);
}

int mpipe_vid_sink_client_init(struct mpipe_vid_sink_client *vsc, uint8_t id,
			       const struct mpipe_vid_rpc_channel *vid_channel)
{
	__ASSERT_NO_MSG(vsc != NULL);
	__ASSERT_NO_MSG(vid_channel != NULL && vid_channel->channel != NULL &&
			vid_channel->format != NULL);

	struct mpipe_element *self = &vsc->sink.sink.element;
	struct mpipe_sink *sink = &vsc->sink.sink;
	struct mpipe_buffer_pool_config req = {0};
	int ret;

	vsc->sink.channel = vid_channel->channel;
	vsc->format = vid_channel->format;

	ret = mpipe_sink_client_init(&vsc->sink, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_sink_client");

	sink->sink_pad.enum_caps_fn = mpipe_vid_sink_client_enum_caps;
	sink->set_caps = mpipe_vid_sink_client_set_caps;

	vsc->sink.in_pool = &vsc->in_pool.pool;
	mpipe_vid_buffer_pool_client_init(vsc->sink.in_pool);

	/* Asked for once; the formats are enumerated from the server on demand */
	if (vsc->format->get_buf_caps(vsc->sink.channel, MPIPE_PAD_SINK, &req.min_buffers,
				      &req.align) != 0) {
		LOG_ERR("Unable to retrieve buffer pool capabilities");
		return -ENODEV;
	}

	return mpipe_buffer_pool_set_req_config(vsc->sink.in_pool, &req);
}

#define DT_DRV_COMPAT zephyr_mpipe_vid_sink_client

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
/* Waits for the server on the other core */
static int mpipe_vid_sink_client_dt_init(const struct mpipe_dt_element *dt)
{
	const struct mpipe_vid_dt_channel *dt_channel = dt->config;
	struct mpipe_vid_rpc_channel vid_channel;
	int ret;

	ret = dt_channel->get(dt_channel, &vid_channel);
	if (ret != 0) {
		return ret;
	}

	return mpipe_vid_sink_client_init((struct mpipe_vid_sink_client *)dt->element, dt->id,
					  &vid_channel);
}

/* The channel is the only property, so its entry is the configuration */
#define MPIPE_VID_SINK_CLIENT_DT_DEFINE(inst)                                                      \
	extern const struct mpipe_vid_dt_channel MPIPE_VID_DT_CHANNEL_NAME(                        \
		DT_INST_PHANDLE(inst, channel));                                                   \
	static struct mpipe_vid_sink_client mpipe_vid_sink_client_##inst;                          \
	MPIPE_DT_INST_ELEMENT_DEFINE(inst, &mpipe_vid_sink_client_##inst,                          \
				     mpipe_vid_sink_client_dt_init,                                \
				     &MPIPE_VID_DT_CHANNEL_NAME(DT_INST_PHANDLE(inst, channel)));

DT_INST_FOREACH_STATUS_OKAY(MPIPE_VID_SINK_CLIENT_DT_DEFINE)
#endif
