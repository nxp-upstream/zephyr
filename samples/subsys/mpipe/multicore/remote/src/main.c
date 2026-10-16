/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Server of the video transform interface: decodes the JPEG frames the
 * pipeline on the other core hands over by address into RGB565.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/cache.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/formats.h>
#include <zephyr/video/types.h>

#include <JPEGDEC.h>

#include "erpc_mbf_setup.h"
#include "erpc_server_setup.h"
#include "erpc_transport_setup.h"
#include "c_mpipe_vid_transform_server.h"
#include "mpipe_vid_transform_common.h"

#define JPEG_DEC_SIZE_MIN  64U
#define JPEG_DEC_SIZE_MAX  8192U
#define JPEG_DEC_SIZE_STEP 16U

#define JPEG_DEC_FORMAT_CAP(pixfmt)                                                                \
	{                                                                                          \
		.pixelformat = (pixfmt),                                                           \
		.width_min = JPEG_DEC_SIZE_MIN,                                                    \
		.width_max = JPEG_DEC_SIZE_MAX,                                                    \
		.height_min = JPEG_DEC_SIZE_MIN,                                                   \
		.height_max = JPEG_DEC_SIZE_MAX,                                                   \
		.width_step = JPEG_DEC_SIZE_STEP,                                                  \
		.height_step = JPEG_DEC_SIZE_STEP,                                                 \
	}

static const mpipe_rpc_video_format_cap in_caps[] = {
	JPEG_DEC_FORMAT_CAP(VIDEO_PIX_FMT_JPEG),
};

static const mpipe_rpc_video_format_cap out_caps[] = {
	JPEG_DEC_FORMAT_CAP(VIDEO_PIX_FMT_RGB565),
};

static mpipe_rpc_video_format out_fmt;
static JPEGIMAGE jpg;

static void error_handler(erpc_status_t err, uint32_t function_id)
{
	if (err == kErpcStatus_Success) {
		return;
	}

	printk("eRPC error %d in function %u\n", (int)err, function_id);
}

int32_t get_buf_caps_rpc(mpipe_rpc_pad_direction direction, uint8_t *min_buffers,
			 uint16_t *buf_align)
{
	ARG_UNUSED(direction);

	*min_buffers = 1;
	/* The decoder reads and writes bytes: no alignment constraint */
	*buf_align = 1;

	return 0;
}

int32_t get_format_caps_rpc(mpipe_rpc_pad_direction direction, uint8_t ind,
			    mpipe_rpc_video_format_cap *vfc)
{
	const mpipe_rpc_video_format_cap *caps;
	uint8_t num;

	if (direction == MPIPE_RPC_PAD_SINK) {
		caps = in_caps;
		num = ARRAY_SIZE(in_caps);
	} else if (direction == MPIPE_RPC_PAD_SRC) {
		caps = out_caps;
		num = ARRAY_SIZE(out_caps);
	} else {
		return -EINVAL;
	}

	if (ind >= num) {
		return -ERANGE;
	}

	*vfc = caps[ind];

	return 0;
}

int32_t set_format_rpc(mpipe_rpc_video_format *fmt)
{
	const mpipe_rpc_video_format_cap *caps;
	uint8_t num;
	uint8_t i;

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		caps = in_caps;
		num = ARRAY_SIZE(in_caps);
	} else if (fmt->type == VIDEO_BUF_TYPE_OUTPUT) {
		caps = out_caps;
		num = ARRAY_SIZE(out_caps);
	} else {
		return -EINVAL;
	}

	for (i = 0; i < num; i++) {
		if (fmt->pixelformat == caps[i].pixelformat && fmt->width >= caps[i].width_min &&
		    fmt->width <= caps[i].width_max && fmt->height >= caps[i].height_min &&
		    fmt->height <= caps[i].height_max) {
			break;
		}
	}

	if (i == num) {
		return -ENOTSUP;
	}

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		/* One byte per pixel bounds the MJPEG frames this sample plays */
		fmt->pitch = 0;
		fmt->size = fmt->width * fmt->height;
	} else {
		fmt->pitch = fmt->width * 2U;
		fmt->size = fmt->pitch * fmt->height;
		out_fmt = *fmt;
	}

	return 0;
}

int32_t transform_cap_rpc(mpipe_rpc_pad_direction direction, uint16_t ind,
			  const mpipe_rpc_video_format_cap *vfc,
			  mpipe_rpc_video_format_cap *other_vfc)
{
	*other_vfc = *vfc;

	if (direction == MPIPE_RPC_PAD_SRC && vfc->pixelformat == VIDEO_PIX_FMT_JPEG &&
	    ind < ARRAY_SIZE(out_caps)) {
		other_vfc->pixelformat = out_caps[ind].pixelformat;
	} else if (direction == MPIPE_RPC_PAD_SINK && vfc->pixelformat == VIDEO_PIX_FMT_RGB565 &&
		   ind < ARRAY_SIZE(in_caps)) {
		other_vfc->pixelformat = in_caps[ind].pixelformat;
	} else {
		return -EINVAL;
	}

	return 0;
}

int32_t process_fn_rpc(uint32_t in_buf, uint32_t in_sz, uint32_t out_buf, uint32_t *out_sz)
{
	/* The frames live in shared memory the other core wrote: bypass this core's cache */
	sys_cache_data_invd_range((void *)(uintptr_t)in_buf, in_sz);

	if (JPEGDEC_openRAM(&jpg, (uint8_t *)(uintptr_t)in_buf, (int)in_sz, NULL) == 0) {
		printk("JPEG open failed\n");
		return -EINVAL;
	}

	JPEGDEC_setFramebuffer(&jpg, (void *)(uintptr_t)out_buf);

	switch (out_fmt.pixelformat) {
	case VIDEO_PIX_FMT_RGB565:
		JPEGDEC_setPixelType(&jpg, JPEGD_PIXEL_RGB565_LE);
		break;
	default:
		JPEGDEC_close(&jpg);
		return -ENOTSUP;
	}

	if (JPEGDEC_decode(&jpg, 0, 0, 0) == 0) {
		printk("JPEG decode failed\n");
		JPEGDEC_close(&jpg);
		return -EIO;
	}

	/* The output is RGB565: two bytes per pixel */
	*out_sz = (uint32_t)JPEGDEC_getWidth(&jpg) * (uint32_t)JPEGDEC_getHeight(&jpg) * 2U;

	/* Push the decoded frame out of this core's cache for the other core */
	sys_cache_data_flush_range((void *)(uintptr_t)out_buf, *out_sz);

	JPEGDEC_close(&jpg);

	return 0;
}

int main(void)
{
	erpc_transport_t transport;
	erpc_mbf_t mbf;
	erpc_server_t server;
	erpc_service_t service;

	printk("Remote started\n");

	transport = erpc_transport_zephyr_ipc_remote_init(CONFIG_ERPC_ENDPOINT_NAME, 0);
	if (transport == NULL) {
		printk("Failed to initialize the eRPC transport\n");
		return 0;
	}

	mbf = erpc_mbf_ipc_service_init(transport);
	server = erpc_server_init(transport, mbf);
	service = create_MpipeVidTransform_service();
	erpc_add_service_to_server(server, service);

	for (;;) {
		erpc_status_t status = erpc_server_poll(server);

		if (status != kErpcStatus_Success) {
			error_handler(status, 0);
			break;
		}
	}

	erpc_remove_service_from_server(server, service);
	destroy_MpipeVidTransform_service(service);
	erpc_server_stop(server);
	erpc_server_deinit(server);

	return 0;
}
