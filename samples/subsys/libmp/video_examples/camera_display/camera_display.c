/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video-controls.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <mp.h>

LOG_MODULE_REGISTER(main);

#define LOG_LEVEL LOG_LEVEL_DBG

#define PIPELINE_ID    0
#define CAM_SRC_ID     1
#define CAPS_FILTER_ID 2
#define DISP_SINK_ID   3

int main(void)
{
	int ret;

	/* Create a new pipeline */
	struct mp_element *pipeline = mp_pipeline_new(PIPELINE_ID);

	if (pipeline == NULL) {
		goto err;
	}

	/* Create elements */
	struct mp_element *source = mp_element_factory_create(MP_ZVID_SRC_ELEM, CAM_SRC_ID);

	if (source == NULL) {
		goto err;
	}

	struct mp_element *caps_filter =
		mp_element_factory_create(MP_CAPS_FILTER_ELEM, CAPS_FILTER_ID);

	if (caps_filter == NULL) {
		goto err;
	}

	struct mp_element *sink = mp_element_factory_create(MP_ZDISP_SINK_ELEM, DISP_SINK_ID);

	if (sink == NULL) {
		goto err;
	}

	/* Set elements' properties */
	ret = mp_object_set_properties(MP_OBJECT(source), PROP_NUM_BUFS, 300, VIDEO_CID_HFLIP, 1,
				       PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	struct mp_caps *filtered_caps = mp_caps_new(
		MP_MEDIA_VIDEO, MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT, 320, MP_CAPS_IMAGE_HEIGHT,
		MP_TYPE_UINT, 240, MP_CAPS_FRAME_RATE, MP_TYPE_UINT_FRACTION, 30, 1, MP_CAPS_END);

	if (filtered_caps == NULL) {
		goto err;
	}

	ret = mp_object_set_properties(MP_OBJECT(caps_filter), PROP_CAPS, filtered_caps,
				       PROP_LIST_END);
	mp_caps_unref(filtered_caps);
	if (ret < 0) {
		goto err;
	}

	/* Add elements to the pipeline - order does not matter */
	if (mp_bin_add(MP_BIN(pipeline), source, caps_filter, sink, NULL) == false) {
		LOG_ERR("Failed to add elements");
		goto err;
	}

	/* Link elements together - order does matter */
	if (mp_element_link(source, caps_filter, sink, NULL) == false) {
		LOG_ERR("Failed to link elements");
		goto err;
	}

	/* Start playing */
	if (mp_element_set_state(pipeline, MP_STATE_PLAYING) != MP_STATE_CHANGE_SUCCESS) {
		LOG_ERR("Failed to start pipeline");
		goto err;
	}

	/* Handle message from the pipeline */
	struct mp_bus *bus = mp_element_get_bus(pipeline);
	/* Wait until an Error or an EOS - blocking */
	struct mp_message *msg = mp_bus_pop_msg(bus, MP_MESSAGE_ERROR | MP_MESSAGE_EOS);

	if (msg != NULL) {
		switch (msg->type) {
		case MP_MESSAGE_ERROR:
			LOG_INF("ERROR message from element %d", msg->src->id);
			break;
		case MP_MESSAGE_EOS:
			LOG_INF("EOS message from element %d", msg->src->id);
			break;
		default:
			LOG_ERR("Unexpected message from element %d", msg->src->id);
			break;
		}
	}

	/* TODO: Stop pipeline and free allocated resources */
	mp_message_destroy(msg);

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
