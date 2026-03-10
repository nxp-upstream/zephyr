/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/devicetree.h>

#include "JPEGDEC.h"

#include "erpc_server_setup.h"
#include "erpc/c_mp_zvid_transform_server.h"
#include "erpc/erpc_error_handler.h"
#include "erpc/mp_zvid_transform_common.h"

#include "rpmsg_lite.h"

/* TODO: rpmsg-lite should define the link ID in devicetree instead of in rpmsg_platform.h */
#define ERPC_TRANSPORT_RPMSG_LITE_LINK_ID (RL_PLATFORM_IMXRT1170_M7_M4_LINK_ID)

#define SHM_MEM_ADDR DT_REG_ADDR(DT_CHOSEN(zephyr_ipc_shm))
#define SHM_MEM_SIZE DT_REG_SIZE(DT_CHOSEN(zephyr_ipc_shm))

/* Video stuffs picked from Zephyr's video.h */
#define VIDEO_FOURCC(a, b, c, d)                                                                   \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define VIDEO_PIX_FMT_JPEG   VIDEO_FOURCC('J', 'P', 'E', 'G')
#define VIDEO_PIX_FMT_RGB565 VIDEO_FOURCC('R', 'G', 'B', 'P')

enum video_buf_type {
	/** input buffer type */
	VIDEO_BUF_TYPE_INPUT = 1,
	/** output buffer type */
	VIDEO_BUF_TYPE_OUTPUT = 2,
};

/* End of copied video stuffs */

#define JPEGDEC_WIDTH_MIN  64U
#define JPEGDEC_WIDTH_MAX  8192U
#define JPEGDEC_WIDTH_STEP 16U

#define JPEGDEC_FORMAT_CAP(pixfmt)                                                                 \
	{                                                                                          \
		.pixelformat = pixfmt,                                                             \
		.width_min = JPEGDEC_WIDTH_MIN,                                                    \
		.width_max = JPEGDEC_WIDTH_MAX,                                                    \
		.width_step = JPEGDEC_WIDTH_STEP,                                                  \
		.height_min = JPEGDEC_WIDTH_MIN,                                                   \
		.height_max = JPEGDEC_WIDTH_MAX,                                                   \
		.height_step = JPEGDEC_WIDTH_STEP,                                                 \
	}

static const video_format_cap jpegdec_in_caps[] = {
	JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_JPEG),
};

static const video_format_cap jpegdec_out_caps[] = {
	JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_RGB565),
};

static video_format out_fmt;
static JPEGIMAGE jpg;

int32_t get_buf_caps_rpc(mp_pad_direction direction, uint8_t *min_buffers, uint16_t *buf_align)
{
	*min_buffers = 1;
	*buf_align = 0;

	return 0;
}

int32_t get_format_caps_rpc(mp_pad_direction direction, uint8_t ind, video_format_cap *vfc)
{
	if (vfc == NULL || (direction != MP_PAD_SINK && direction != MP_PAD_SRC)) {
		return -EINVAL;
	}

	const video_format_cap *jcaps =
		(direction == MP_PAD_SINK) ? jpegdec_in_caps : jpegdec_out_caps;
	uint8_t size = (direction == MP_PAD_SINK) ? ARRAY_SIZE(jpegdec_in_caps)
						  : ARRAY_SIZE(jpegdec_out_caps);

	if (ind >= size) {
		return -ERANGE;
	}

	*vfc = jcaps[ind];

	return 0;
}

int32_t set_format_rpc(video_format *fmt)
{
	if (fmt->type != VIDEO_BUF_TYPE_INPUT && fmt->type != VIDEO_BUF_TYPE_OUTPUT) {
		return -EINVAL;
	}

	const video_format_cap *vfc =
		(fmt->type == VIDEO_BUF_TYPE_INPUT) ? jpegdec_in_caps : jpegdec_out_caps;
	uint8_t sz = (fmt->type == VIDEO_BUF_TYPE_INPUT) ? ARRAY_SIZE(jpegdec_in_caps)
							 : ARRAY_SIZE(jpegdec_out_caps);
	uint8_t i;

	for (i = 0; i < sz; i++) {
		if (fmt->pixelformat == vfc[i].pixelformat && fmt->width > vfc[i].width_min &&
		    fmt->width < vfc[i].width_max && fmt->height > vfc[i].height_min &&
		    fmt->height < vfc[i].height_max) {
			break;
		}
	}

	if (i == sz) {
		return -ENOTSUP;
	}

	/* TODO: Generalize to BPP and size config */
	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		fmt->pitch = 0;
		fmt->size = 1314;
	} else {
		fmt->pitch = fmt->width * 2;
		fmt->size = fmt->pitch * fmt->height;
		out_fmt = *fmt;
	}

	return 0;
}

int transform_cap_rpc(mp_pad_direction direction, uint16_t ind, const video_format_cap *vfc,
		      video_format_cap *other_vfc)
{
	*other_vfc = *vfc;

	/* TODO: in range for output formats like PxP */
	if (direction == MP_PAD_SRC && vfc->pixelformat == VIDEO_PIX_FMT_JPEG &&
	    ind < ARRAY_SIZE(jpegdec_out_caps)) {
		other_vfc->pixelformat = jpegdec_out_caps[ind].pixelformat;
	} else if (direction == MP_PAD_SINK && vfc->pixelformat == VIDEO_PIX_FMT_RGB565 &&
		   ind < ARRAY_SIZE(jpegdec_in_caps)) {
		other_vfc->pixelformat = jpegdec_in_caps[ind].pixelformat;
	} else {
		return -EINVAL;
	}

	return 0;
}

bool chainfn_rpc(uint32_t in_buf, uint32_t in_sz, uint32_t out_buf, uint32_t *out_sz)
{
	if (JPEG_openRAM(&jpg, (uint8_t *)in_buf, in_sz, NULL) == 0) {
		printk("\r\nJPEG open failed\n");
		return false;
	}

	JPEG_setFramebuffer(&jpg, (void *)out_buf);

	/* TODO: Support more formats */
	switch (out_fmt.pixelformat) {
	case VIDEO_PIX_FMT_RGB565:
		JPEG_setPixelType(&jpg, RGB565_LITTLE_ENDIAN);
		break;
	default:
		return false;
	}

	if (JPEG_decode(&jpg, 0, 0, 0) == 0) {
		printk("\r\nJPEG decode failed\n");
		return false;
	}

	*out_sz = JPEG_getWidth(&jpg) * JPEG_getHeight(&jpg) * JPEG_getBpp(&jpg) / 8;

	return true;
}

static void SignalReady(void)
{
}

int main(void)
{
	printk("\r\nRemote started\n");
	/* TODO: 101U, 100U */
	erpc_transport_t transport = erpc_transport_rpmsg_lite_rtos_remote_init(
		101U, 100U, (void *)SHM_MEM_ADDR, ERPC_TRANSPORT_RPMSG_LITE_LINK_ID, SignalReady,
		NULL);
	erpc_mbf_t message_buffer_factory = erpc_mbf_rpmsg_init(transport);
	erpc_server_t server = erpc_server_init(transport, message_buffer_factory);
	erpc_service_t service = create_MPService_service();

	erpc_add_service_to_server(server, service);

	for (;;) {
		/* process message */
		erpc_status_t status = erpc_server_poll(server);

		/* handle error status */
		if (status != (erpc_status_t)kErpcStatus_Success) {
			/* print error description */
			erpc_error_handler(status, 0);

			/* removing the service from the server */
			erpc_remove_service_from_server(server, service);
			destroy_MPService_service(service); //

			/* stop erpc server */
			erpc_server_stop(server);

			/* print error description */
			erpc_server_deinit(server);

			/* exit program loop */
			break;
		}
	}
}
