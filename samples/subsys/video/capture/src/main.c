/*
 * Copyright (c) 2019 Linaro Limited
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * Include transform.h unconditionally to ensure it's compiled and checked by CI,
 * even though the functions in this header are not used here at all.
 * This prevents silent breakage when APIs change.
 */
#include "transform.h"

/*
 * The video source is either a camera described in the devicetree, or a USB webcam attached to
 * the USB host controller. The two cases need different symbols to even exist, hence the
 * preprocessor rather than IS_ENABLED().
 */
#if defined(CONFIG_USBH_VIDEO_CLASS)

#include <zephyr/usb/usbh.h>

USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

/* Video device name registered by the USB host video class for its first instance */
#define APP_UVC_DEVICE_NAME "usbh_uvc_0"

static inline const struct device *app_get_camera_dev(void)
{
	return device_get_binding(APP_UVC_DEVICE_NAME);
}

static int app_start_camera_bus(void)
{
	int ret;

	ret = usbh_init(&uhs_ctx);
	if (ret < 0) {
		LOG_ERR("Failed to initialize USB host support");
		return ret;
	}

	ret = usbh_enable(&uhs_ctx);
	if (ret < 0) {
		LOG_ERR("Failed to enable USB host support");
		return ret;
	}

	return 0;
}

/*
 * A webcam is only usable once it has been plugged in and enumerated, which happens
 * asynchronously. The class driver reports a format as soon as the device is attached.
 */
static void app_wait_for_camera(const struct device *const camera_dev)
{
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};

	LOG_INF("Waiting for a USB video device to be attached");

	while (video_get_format(camera_dev, &fmt) != 0) {
		k_sleep(K_MSEC(100));
	}

	LOG_INF("USB video device attached");
}

#else /* !CONFIG_USBH_VIDEO_CLASS */

#if !DT_HAS_CHOSEN(zephyr_camera)
#error No camera chosen in devicetree. Missing "--shield" or "--snippet video-sw-generator" flag?
#endif

static inline const struct device *app_get_camera_dev(void)
{
	return DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
}

static inline int app_start_camera_bus(void)
{
	return 0;
}

static inline void app_wait_for_camera(const struct device *const camera_dev)
{
	ARG_UNUSED(camera_dev);
}

#endif /* CONFIG_USBH_VIDEO_CLASS */

/* The default transform implementation is a pass-through when no transform device is available */
int __weak app_setup_video_transform(const struct device *const transform_dev,
				     struct video_format *const in_fmt,
				     struct video_format *const out_fmt,
				     struct video_buffer **out_buf)
{
	*out_fmt = *in_fmt;

	return 0;
}

int __weak app_transform_frame(const struct device *const transform_dev,
			       struct video_buffer *in_buf, struct video_buffer **out_buf)
{
	*out_buf = in_buf;

	return 0;
}

void __weak app_teardown_video_transform(struct video_buffer **out_buf)
{
	ARG_UNUSED(out_buf);
}

/* Lines of black pushed to the display at a time when clearing it */
#define APP_CLEAR_BAND_LINES 16

/*
 * Paint the whole panel black once. A video source smaller than the panel only ever covers
 * part of it, and a display controller comes out of reset with whatever its memory happened
 * to contain, which would otherwise stay visible around the image.
 */
static void app_clear_display(const struct device *const display_dev,
			      const struct display_capabilities *const caps)
{
	struct display_buffer_descriptor buf_desc = {
		.width = caps->x_resolution,
		.pitch = caps->x_resolution,
	};
	struct video_buffer *vbuf;
	size_t bytes_per_pixel;
	uint16_t band;
	int ret;

	ret = display_clear(display_dev);
	if (ret == 0) {
		return;
	}
	if (ret != -ENOSYS) {
		LOG_WRN("Failed to clear the display: %d", ret);
		return;
	}

	/* The driver has no clear operation, write black over the panel instead */
	bytes_per_pixel = DISPLAY_BITS_PER_PIXEL(caps->current_pixel_format) / BITS_PER_BYTE;
	if (bytes_per_pixel == 0) {
		LOG_WRN("Unknown display pixel format, not clearing the display");
		return;
	}

	band = MIN(APP_CLEAR_BAND_LINES, caps->y_resolution);

	vbuf = video_buffer_aligned_alloc((size_t)caps->x_resolution * band * bytes_per_pixel,
					  CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
	if (vbuf == NULL) {
		LOG_WRN("Not enough video pool memory to clear the display");
		return;
	}

	memset(vbuf->buffer, 0, vbuf->size);

	for (uint16_t y = 0; y < caps->y_resolution; y += band) {
		buf_desc.height = MIN(band, caps->y_resolution - y);
		buf_desc.buf_size = (size_t)buf_desc.height * caps->x_resolution * bytes_per_pixel;

		ret = display_write(display_dev, 0, y, &buf_desc, vbuf->buffer);
		if (ret != 0) {
			LOG_WRN("Failed to clear the display at line %u: %d", y, ret);
			break;
		}
	}

	video_buffer_release(vbuf);
}

static inline int app_setup_display(const struct device *const display_dev, const uint32_t pixfmt)
{
	struct display_capabilities capabilities;
	int ret = 0;

	LOG_INF("Display device: %s", display_dev->name);

	if (!device_is_ready(display_dev)) {
		LOG_ERR("%s: display device not ready.", display_dev->name);
		return -ENOSYS;
	}

	display_get_capabilities(display_dev, &capabilities);

	LOG_INF("- Capabilities:");
	LOG_INF("  x_resolution = %u, y_resolution = %u, supported_pixel_formats = %u"
		"  current_pixel_format = %u, current_orientation = %u",
		capabilities.x_resolution, capabilities.y_resolution,
		capabilities.supported_pixel_formats, capabilities.current_pixel_format,
		capabilities.current_orientation);

	/* Set display pixel format to match the one in use by the camera */
	switch (pixfmt) {
	case VIDEO_PIX_FMT_RGB565:
		if (capabilities.current_pixel_format != PIXEL_FORMAT_RGB_565) {
			ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);
		}
		break;
	case VIDEO_PIX_FMT_BGRX32:
		if (capabilities.current_pixel_format != PIXEL_FORMAT_XRGB_8888) {
			ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_XRGB_8888);
			if (ret < 0) {
				/* If failed with PIXEL_FORMAT_XRGB_8888, PIXEL_FORMAT_ARGB_8888 is
				 * still applicable
				 */
				ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_ARGB_8888);
			}
		}
		break;
	case VIDEO_PIX_FMT_RGBA32:
		if (capabilities.current_pixel_format != PIXEL_FORMAT_ABGR_8888) {
			ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_ABGR_8888);
		}
		break;
	default:
		LOG_ERR("Display pixel format not supported by this sample");
		return -ENOTSUP;
	}
	if (ret < 0) {
		LOG_ERR("Unable to set display format");
		return ret;
	}

	/* Turn off blanking if driver supports it */
	ret = display_blanking_off(display_dev);
	if (ret == -ENOSYS) {
		LOG_DBG("Display blanking off not available");
		ret = 0;
	}
	if (ret < 0) {
		return ret;
	}

	/* Read the capabilities again to pick up the pixel format that was just set */
	display_get_capabilities(display_dev, &capabilities);
	app_clear_display(display_dev, &capabilities);

	return 0;
}

static int app_display_frame(const struct device *const display_dev,
			     const struct video_buffer *const vbuf,
			     const struct video_format *const fmt)
{
	struct display_buffer_descriptor buf_desc = {
		.buf_size = vbuf->bytesused,
		.width = fmt->width,
		.pitch = buf_desc.width,
		.height = vbuf->bytesused / fmt->pitch,
	};

	return display_write(display_dev, 0, vbuf->line_offset, &buf_desc, vbuf->buffer);
}

static int app_setup_video_selection(const struct device *const camera_dev,
				     const struct video_format *const fmt)
{
	struct video_selection sel = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	/* Set the crop setting only if configured */
	if (CONFIG_VIDEO_SOURCE_CROP_WIDTH > 0) {
		sel.target = VIDEO_SEL_TGT_CROP;
		sel.rect.left = CONFIG_VIDEO_SOURCE_CROP_LEFT;
		sel.rect.top = CONFIG_VIDEO_SOURCE_CROP_TOP;
		sel.rect.width = CONFIG_VIDEO_SOURCE_CROP_WIDTH;
		sel.rect.height = CONFIG_VIDEO_SOURCE_CROP_HEIGHT;

		ret = video_set_selection(camera_dev, &sel);
		if (ret < 0) {
			LOG_ERR("Unable to set selection crop");
			return ret;
		}

		LOG_INF("Crop window set to (%u,%u)/%ux%u",
			sel.rect.left, sel.rect.top, sel.rect.width, sel.rect.height);
	}

	return 0;
}

static int app_query_video_info(const struct device *const camera_dev,
				struct video_caps *const caps,
				struct video_format *const fmt)
{
	int ret;

	LOG_INF("Camera device: %s", camera_dev->name);

	if (!device_is_ready(camera_dev)) {
		LOG_ERR("%s: camera device is not ready", camera_dev->name);
		return -ENOSYS;
	}

	/* Get capabilities */
	ret = video_get_caps(camera_dev, caps);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video capabilities");
		return ret;
	}

	LOG_INF("- Capabilities:");
	for (int i = 0; caps->format_caps[i].pixelformat; i++) {
		const struct video_format_cap *fcap = &caps->format_caps[i];

		LOG_INF("  %s width [%u; %u; %u] height [%u; %u; %u]",
			VIDEO_FOURCC_TO_STR(fcap->pixelformat),
			fcap->width_min, fcap->width_max, fcap->width_step,
			fcap->height_min, fcap->height_max, fcap->height_step);
	}

	/* Get default/native format */
	ret = video_get_format(camera_dev, fmt);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video format");
	}

	/* Adjust video format according to the configuration */
	if (CONFIG_VIDEO_FRAME_HEIGHT > 0) {
		fmt->height = CONFIG_VIDEO_FRAME_HEIGHT;
	}
	if (CONFIG_VIDEO_FRAME_WIDTH > 0) {
		fmt->width = CONFIG_VIDEO_FRAME_WIDTH;
	}
	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		fmt->pixelformat = VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT);
	}

	return 0;
}

static int app_setup_video_format(const struct device *const camera_dev,
				  struct video_format *const fmt)
{
	int ret;

	LOG_INF("- Video format: %s %ux%u",
		VIDEO_FOURCC_TO_STR(fmt->pixelformat), fmt->width, fmt->height);

	ret = video_set_compose_format(camera_dev, fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format");
		return ret;
	}

	return 0;
}

static int app_setup_video_frmival(const struct device *const camera_dev,
				   struct video_format *const fmt)
{
	struct video_frmival frmival = {};
	struct video_frmival_enum fie = {
		.format = fmt,
	};
	int ret;

	LOG_INF("- Supported frame intervals for the default format:");

	while (video_enum_frmival(camera_dev, &fie) == 0) {
		if (fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
			LOG_INF("   %u/%u", fie.discrete.numerator, fie.discrete.denominator);
		} else {
			LOG_INF("   [min = %u/%u; max = %u/%u; step = %u/%u]",
				fie.stepwise.min.numerator, fie.stepwise.min.denominator,
				fie.stepwise.max.numerator, fie.stepwise.max.denominator,
				fie.stepwise.step.numerator, fie.stepwise.step.denominator);
		}
		fie.index++;
	}

	ret = video_get_frmival(camera_dev, &frmival);
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		LOG_WRN("The video source does not support frame rate control");
	} else if (ret < 0) {
		LOG_ERR("Error while getting the frame interval");
		return ret;
	} else if (ret == 0) {
		LOG_INF("- Default frame rate : %f fps",
			1.0 * frmival.denominator / frmival.numerator);
	}

	return 0;
}

static int app_setup_video_controls(const struct device *const camera_dev)
{
	int ret;

	/* Get supported controls */
	LOG_INF("- Supported controls:");
	const struct device *last_dev = NULL;
	struct video_ctrl_query cq = {.dev = camera_dev, .id = VIDEO_CTRL_FLAG_NEXT_CTRL};

	while (video_query_ctrl(&cq) == 0) {
		if (cq.dev != last_dev) {
			last_dev = cq.dev;
			LOG_INF("\t\tdevice: %s", cq.dev->name);
		}
		video_print_ctrl(&cq);
		cq.id |= VIDEO_CTRL_FLAG_NEXT_CTRL;
	}

	/* Set controls */
	struct video_control ctrl = {.id = VIDEO_CID_HFLIP, .val = 1};

	if (IS_ENABLED(CONFIG_VIDEO_CTRL_HFLIP)) {
		ret = video_set_ctrl(camera_dev, &ctrl);
		if (ret < 0) {
			LOG_ERR("Failed to set horizontal flip");
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_VIDEO_CTRL_VFLIP)) {
		ctrl.id = VIDEO_CID_VFLIP;
		ret = video_set_ctrl(camera_dev, &ctrl);
		if (ret < 0) {
			LOG_ERR("Failed to set vertical flip");
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_TEST)) {
		ctrl.id = VIDEO_CID_TEST_PATTERN;
		ret = video_set_ctrl(camera_dev, &ctrl);
		if (ret < 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set the test pattern");
		}
	}

	return 0;
}

static struct video_buffer *app_camera_vbufs[CONFIG_VIDEO_CAM_NUM_BUFS];

static void app_release_video_buffers(void)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(app_camera_vbufs); i++) {
		if (app_camera_vbufs[i] != NULL) {
			video_buffer_release(app_camera_vbufs[i]);
			app_camera_vbufs[i] = NULL;
		}
	}
}

static int app_setup_video_buffers(const struct device *const camera_dev,
				   struct video_caps *const caps,
				   struct video_format *const fmt)
{
	int ret;

	/* Alloc video buffers and enqueue for capture */
	if (caps->min_vbuf_count > CONFIG_VIDEO_CAM_NUM_BUFS) {
		LOG_ERR("Not enough buffers to start streaming");
		return -EINVAL;
	}

	for (uint8_t i = 0; i < CONFIG_VIDEO_CAM_NUM_BUFS; i++) {
		struct video_buffer *vbuf;

		/*
		 * For some hardwares, such as the PxP used on i.MX RT1170 to do image rotation,
		 * buffer alignment is needed in order to achieve the best performance
		 */
		vbuf = video_buffer_aligned_alloc(fmt->size, CONFIG_VIDEO_BUFFER_POOL_ALIGN,
						  K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("Unable to alloc video buffer");
			return -ENOMEM;
		}

		app_camera_vbufs[i] = vbuf;
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;

		ret = video_enqueue(camera_dev, vbuf);
		if (ret < 0) {
			LOG_ERR("Failed to enqueue video buffer");
			return ret;
		}
	}

	return 0;
}

/*
 * Configure the camera, stream frames to the display until the camera goes away, then release
 * everything that was set up so that a new session can start from scratch.
 */
static int app_capture_session(const struct device *const camera_dev,
			       const struct device *const display_dev,
			       const struct device *const transform_dev)
{
	struct video_buffer *camera_vbuf = &(struct video_buffer){};
	struct video_buffer *transformed_vbuf = &(struct video_buffer){};
	struct video_format camera_fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	struct video_format transformed_fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	unsigned int frame = 0;
	uint32_t last_ts = 0;
	int ret;

	ret = app_query_video_info(camera_dev, &caps, &camera_fmt);
	if (ret < 0) {
		goto out;
	}

	ret = app_setup_video_selection(camera_dev, &camera_fmt);
	if (ret < 0) {
		goto out;
	}

	ret = app_setup_video_format(camera_dev, &camera_fmt);
	if (ret < 0) {
		goto out;
	}

	ret = app_setup_video_frmival(camera_dev, &camera_fmt);
	if (ret < 0) {
		goto out;
	}

	ret = app_setup_video_controls(camera_dev);
	if (ret < 0) {
		goto out;
	}

	ret = app_setup_video_transform(transform_dev, &camera_fmt, &transformed_fmt,
					&transformed_vbuf);
	if (ret < 0) {
		LOG_ERR("Unable to setup video transform");
		goto out;
	}

	if (DT_HAS_CHOSEN(zephyr_display)) {
		ret = app_setup_display(display_dev, transformed_fmt.pixelformat);
		if (ret < 0) {
			goto out;
		}
	}

	ret = app_setup_video_buffers(camera_dev, &caps, &camera_fmt);
	if (ret < 0) {
		goto out;
	}

	ret = video_stream_start(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Unable to start capture (interface)");
		goto out;
	}

	LOG_INF("Capture started");

	camera_vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
	while (1) {
		ret = video_dequeue(camera_dev, &camera_vbuf, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("Unable to dequeue video buf");
			break;
		}

		LOG_INF("Got frame %u! size: %u; timestamp %u ms (delta %u ms)", frame++,
			camera_vbuf->bytesused, camera_vbuf->timestamp,
			camera_vbuf->timestamp - last_ts);
		last_ts = camera_vbuf->timestamp;

		ret = app_transform_frame(transform_dev, camera_vbuf, &transformed_vbuf);
		if (ret < 0) {
			LOG_ERR("Unable to transform video frame");
			break;
		}

		if (DT_HAS_CHOSEN(zephyr_display)) {
			ret = app_display_frame(display_dev, transformed_vbuf, &transformed_fmt);
			if (ret != 0) {
				LOG_WRN("Failed to display this frame");
			}
		}

		ret = video_enqueue(camera_dev, camera_vbuf);
		if (ret < 0) {
			LOG_ERR("Unable to requeue video buf");
			break;
		}
	}

	/* -ENODEV is expected once the camera is detached, the stream is already gone */
	if (video_stream_stop(camera_dev, VIDEO_BUF_TYPE_OUTPUT) < 0) {
		LOG_WRN("Unable to stop capture (interface)");
	}

out:
	app_teardown_video_transform(&transformed_vbuf);
	app_release_video_buffers();

	return ret;
}

int main(void)
{
	const struct device *const camera_dev = app_get_camera_dev();
	const struct device *const display_dev = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_display));
	const struct device *transform_dev = NULL;
	int ret;

	/* When the video shell is enabled, do not run the capture loop unless requested */
	if (IS_ENABLED(CONFIG_VIDEO_SHELL) && !IS_ENABLED(CONFIG_VIDEO_SHELL_AND_CAPTURE)) {
		LOG_INF("Letting the user control the device with the video shell");
		return 0;
	}

	if (camera_dev == NULL || !device_is_ready(camera_dev)) {
		LOG_ERR("Video source device is not ready");
		return 0;
	}

	ret = app_start_camera_bus();
	if (ret < 0) {
		goto err;
	}

	transform_dev = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_videotrans));
	if (transform_dev == NULL) {
		transform_dev = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_videodec));
	}

	do {
		app_wait_for_camera(camera_dev);

		ret = app_capture_session(camera_dev, display_dev, transform_dev);

		/*
		 * A detached camera is not an error for a hotpluggable video source: wait for the
		 * next one instead of giving up.
		 */
		if (IS_ENABLED(CONFIG_USBH_VIDEO_CLASS) && ret == -ENODEV) {
			LOG_INF("Video source detached, waiting for it to come back");
		}
	} while (IS_ENABLED(CONFIG_USBH_VIDEO_CLASS) && ret == -ENODEV);

err:
	LOG_ERR("Aborting sample");
	return 0;
}
