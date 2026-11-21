/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

#include <zephyr/mpipe/vid/mpipe_vid_h264_encoder.h>

#include <minih264e.h>

/* Investigation aid, see mpipe_stage_stats.h */
#define MPIPE_STAGE_STATS IS_ENABLED(CONFIG_MPIPE_STAGE_STATS)
#if MPIPE_STAGE_STATS
#include "../mpipe_stage_stats.h"
static struct mpipe_stage_stats enc_stats;
#endif

LOG_MODULE_REGISTER(mpipe_vid_h264_encoder, CONFIG_MPIPE_LOG_LEVEL);

#define H264_ENCODER_QP_MIN        10U
#define H264_ENCODER_QP_MAX        51U
#define H264_ENCODER_QP_DEFAULT    30U
#define H264_ENCODER_GOP_DEFAULT   30U
#define H264_ENCODER_SPEED_MAX     10U
/* The fastest setting that keeps the deblocking filter */
#define H264_ENCODER_SPEED_DEFAULT 9U

/* Internal output pool (used when downstream doesn't propose a pool) */
NET_BUF_POOL_FIXED_DEFINE(mpipe_vid_h264_enc_pool, CONFIG_MPIPE_VID_H264_ENCODER_POOL_NUM,
			  CONFIG_MPIPE_VID_H264_ENCODER_MAX_OUT_FRAME_SIZE,
			  sizeof(struct mpipe_buffer_meta), mpipe_buffer_destroy);

/*
 * The encoder state and its working area. Their sizes depend on the frame
 * size and are only known from H264E_sizeof(), so they are reserved for the
 * largest frame the application configures, for one element.
 */
static uint8_t h264_encoder_persist[CONFIG_MPIPE_VID_H264_ENCODER_PERSIST_SIZE] __aligned(64);
static uint8_t h264_encoder_scratch[CONFIG_MPIPE_VID_H264_ENCODER_SCRATCH_SIZE] __aligned(64);
/* The element the memory serves: the last one initialized */
static struct mpipe_vid_h264_encoder *h264_encoder_owner;

static int mpipe_vid_h264_encoder_out_pool_acquire(struct mpipe_buffer_pool *pool,
						   struct net_buf **buf)
{
	struct net_buf *out;
	struct mpipe_buffer_meta *m;

	__ASSERT_NO_MSG(pool != NULL);
	__ASSERT_NO_MSG(buf != NULL);

	out = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_NO_WAIT);
	if (out == NULL) {
		return -ENOBUFS;
	}

	m = mpipe_buffer_get_meta(out);
	m->pool = pool;
	m->bytes_used = 0;
	m->pts = 0;
	m->driver_buf = NULL;
	m->priv = NULL;
	out->len = 0;

	*buf = out;

	return 0;
}

static int mpipe_vid_h264_encoder_out_pool_release(struct mpipe_buffer_pool *pool,
						   struct net_buf *buf)
{
	ARG_UNUSED(pool);
	ARG_UNUSED(buf);

	return 0;
}

static void mpipe_vid_h264_encoder_out_pool_init(struct mpipe_vid_h264_encoder *enc)
{
	const struct mpipe_buffer_pool_config pool_req = {
		.size = CONFIG_MPIPE_VID_H264_ENCODER_MAX_OUT_FRAME_SIZE,
		.align = 1,
		.min_buffers = CONFIG_MPIPE_VID_H264_ENCODER_POOL_NUM,
		.max_buffers = CONFIG_MPIPE_VID_H264_ENCODER_POOL_NUM,
	};

	mpipe_buffer_pool_init(&enc->out_pool);
	enc->out_pool.nb_pool = &mpipe_vid_h264_enc_pool;
	(void)mpipe_buffer_pool_set_req_config(&enc->out_pool, &pool_req);
	enc->out_pool.acquire_buffer = mpipe_vid_h264_encoder_out_pool_acquire;
	enc->out_pool.release_buffer = mpipe_vid_h264_encoder_out_pool_release;
	enc->out_pool.started = true; /* net_buf pool is static; no explicit start */
}

/* Create the encoder for the negotiated frame size */
static int mpipe_vid_h264_encoder_create(struct mpipe_vid_h264_encoder *enc)
{
	H264E_create_param_t create = {
		.width = enc->width,
		.height = enc->height,
		.gop = enc->gop,
		/* The input frame belongs to upstream: leave it as it is */
		.const_input_flag = 1,
	};
	int persist_size = 0;
	int scratch_size = 0;
	int ret;

	enc->ready = false;

	if (h264_encoder_owner != enc) {
		LOG_ERR("The encoder memory serves one element at a time");
		return -EBUSY;
	}

	ret = H264E_sizeof(&create, &persist_size, &scratch_size);
	if (ret != H264E_STATUS_SUCCESS) {
		LOG_ERR("A %ux%u frame cannot be encoded (%d)", enc->width, enc->height, ret);
		return -ENOTSUP;
	}

	if (persist_size > (int)sizeof(h264_encoder_persist) ||
	    scratch_size > (int)sizeof(h264_encoder_scratch)) {
		LOG_ERR("A %ux%u frame needs %d and %d bytes of encoder memory, the configuration "
			"reserves %zu and %zu",
			enc->width, enc->height, persist_size, scratch_size,
			sizeof(h264_encoder_persist), sizeof(h264_encoder_scratch));
		return -ENOMEM;
	}

	ret = H264E_init((H264E_persist_t *)h264_encoder_persist, &create);
	if (ret != H264E_STATUS_SUCCESS) {
		LOG_ERR("Encoder creation failed (%d)", ret);
		return -EIO;
	}

	enc->ready = true;

	return 0;
}

static int mpipe_vid_h264_encoder_encode_one(struct mpipe_vid_h264_encoder *enc,
					     struct net_buf *in_buf, struct net_buf *out_buf)
{
	uint32_t luma_size = (uint32_t)enc->width * enc->height;
	H264E_run_param_t run = {
		.encode_speed = enc->speed,
		.qp_min = enc->qp,
		.qp_max = enc->qp,
	};
	H264E_io_yuv_t yuv;
	unsigned char *coded = NULL;
	int coded_size = 0;
	int ret;

	if (mpipe_buffer_get_meta(in_buf)->bytes_used < luma_size * 3U / 2U) {
		LOG_WRN("Short frame: %u bytes for %ux%u",
			mpipe_buffer_get_meta(in_buf)->bytes_used, enc->width, enc->height);
		return -EINVAL;
	}

	if (atomic_cas(&enc->key_frame, 1, 0)) {
		run.frame_type = H264E_FRAME_TYPE_KEY;
	}

	yuv.yuv[0] = in_buf->data;
	yuv.yuv[1] = in_buf->data + luma_size;
	yuv.yuv[2] = in_buf->data + luma_size + luma_size / 4U;
	yuv.stride[0] = enc->width;
	yuv.stride[1] = enc->width / 2;
	yuv.stride[2] = enc->width / 2;

	ret = H264E_encode((H264E_persist_t *)h264_encoder_persist,
			   (H264E_scratch_t *)h264_encoder_scratch, &run, &yuv, &coded,
			   &coded_size);
	if (ret != H264E_STATUS_SUCCESS || coded_size <= 0) {
		LOG_WRN("Encoding failed (%d)", ret);
		return -EIO;
	}

	if ((uint32_t)coded_size > out_buf->size) {
		/*
		 * The frames after this one refer to it, so the receiver cannot
		 * go on without it: start again from a key frame.
		 */
		LOG_WRN("A %d-byte frame does not fit in the %u-byte output buffer", coded_size,
			out_buf->size);
		atomic_set(&enc->key_frame, 1);
		return -ENOBUFS;
	}

	memcpy(out_buf->data, coded, (size_t)coded_size);
	mpipe_buffer_get_meta(out_buf)->bytes_used = (uint32_t)coded_size;
	out_buf->len = (uint32_t)coded_size;

	return 0;
}

static int mpipe_vid_h264_encoder_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					     struct net_buf **out_buf)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)pad->object.container;
	struct mpipe_vid_h264_encoder *enc = (struct mpipe_vid_h264_encoder *)transform;
	struct mpipe_buffer_pool *out_pool = transform->out_pool;
	struct net_buf *out = NULL;
	int ret;

	*out_buf = NULL;

	if (out_pool == NULL) {
		out_pool = &enc->out_pool;
	}

	if (in_buf == NULL || out_pool->acquire_buffer == NULL) {
		return -EINVAL;
	}

	if (!enc->ready) {
		LOG_ERR("Missing negotiated caps");
		net_buf_unref(in_buf);
		return -EINVAL;
	}

	/*
	 * Downstream still holds every output buffer: skip this frame. The
	 * encoder did not see it, so the stream stays decodable.
	 */
	ret = out_pool->acquire_buffer(out_pool, &out);
	if (ret != 0 || out == NULL) {
		LOG_WRN("No output buffer free, dropping a frame (%d)", ret);
		net_buf_unref(in_buf);
		return 0;
	}

#if MPIPE_STAGE_STATS
	uint32_t t0 = k_cycle_get_32();
#endif

	ret = mpipe_vid_h264_encoder_encode_one(enc, in_buf, out);

#if MPIPE_STAGE_STATS
	if (ret != 0) {
		enc_stats.aux++;
	}
	if (mpipe_stage_stats_add(&enc_stats, 1, out->len, out->len,
				  mpipe_stage_stats_elapsed_us(t0))) {
		LOG_INF("stats h264_encoder: %u fr/s, out avg %u max %u B, "
			"encode %u/%u/%u us min/avg/max, fail %u (%u ms)",
			mpipe_stage_stats_per_s(&enc_stats, enc_stats.count),
			enc_stats.bytes / enc_stats.count, enc_stats.bytes_max, enc_stats.us_min,
			mpipe_stage_stats_us_avg(&enc_stats), enc_stats.us_max, enc_stats.aux,
			enc_stats.elapsed_ms);
		mpipe_stage_stats_reset(&enc_stats);
	}
#endif

	if (ret != 0) {
		/* One frame is lost, the stream goes on */
		net_buf_unref(in_buf);
		net_buf_unref(out);
		return 0;
	}

	mpipe_buffer_get_meta(out)->pts = mpipe_buffer_get_meta(in_buf)->pts;
	net_buf_unref(in_buf);

	*out_buf = out;

	return 0;
}

static int mpipe_vid_h264_encoder_candidate(uint32_t pixfmt, struct mpipe_structure *out)
{
	return mpipe_structure_init_fields(out, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					   MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_END);
}

static int mpipe_vid_h264_encoder_enum_caps(struct mpipe_pad *pad, uint32_t index,
					    const struct mpipe_structure *filter,
					    struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	int ret;

	if (index > 0U) {
		return -ENOENT;
	}

	ret = mpipe_vid_h264_encoder_candidate(
		(pad->direction == MPIPE_PAD_SINK) ? VIDEO_PIX_FMT_YUV420 : VIDEO_PIX_FMT_H264,
		&candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_vid_h264_encoder_transform_caps(struct mpipe_transform *transform,
						 enum mpipe_pad_direction direction,
						 const struct mpipe_structure *in, uint32_t index,
						 struct mpipe_structure *out)
{
	/* Encoding changes the format, not the geometry or the timing */
	static const uint8_t passthrough[] = {MPIPE_CAPS_IMAGE_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT,
					      MPIPE_CAPS_FRAME_INTERVAL};
	int ret;

	ARG_UNUSED(transform);

	if (in == NULL) {
		return -EINVAL;
	}

	if (index > 0U) {
		return -ENOENT;
	}

	if (mpipe_structure_is_any(in)) {
		/* Constrains nothing, so neither does the transformation of it */
		return mpipe_structure_init_any(out);
	}

	ret = mpipe_vid_h264_encoder_candidate(
		(direction == MPIPE_PAD_SRC) ? VIDEO_PIX_FMT_H264 : VIDEO_PIX_FMT_YUV420, out);
	if (ret != 0) {
		return ret;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(passthrough); i++) {
		ret = mpipe_structure_copy_field(in, out, passthrough[i]);
		if (ret != 0 && ret != -ENOENT) {
			mpipe_structure_clear(out);
			return ret;
		}
	}

	return 0;
}

static int mpipe_vid_h264_encoder_set_caps(struct mpipe_transform *transform,
					   enum mpipe_pad_direction direction,
					   const struct mpipe_structure *caps)
{
	struct mpipe_vid_h264_encoder *enc = (struct mpipe_vid_h264_encoder *)transform;
	const struct mpipe_value *width;
	const struct mpipe_value *height;
	int ret;

	if (caps == NULL) {
		return -EINVAL;
	}

	ret = mpipe_transform_set_caps(transform, direction, caps);
	if (ret < 0 || direction != MPIPE_PAD_SINK) {
		return ret;
	}

	width = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_WIDTH);
	height = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_HEIGHT);
	if (width == NULL || width->type != MPIPE_TYPE_UINT || height == NULL ||
	    height->type != MPIPE_TYPE_UINT) {
		LOG_ERR("The input format carries no frame size");
		return -EINVAL;
	}

	enc->width = (uint16_t)mpipe_value_get_uint(width);
	enc->height = (uint16_t)mpipe_value_get_uint(height);

	return mpipe_vid_h264_encoder_create(enc);
}

static int mpipe_vid_h264_encoder_decide_buffer_pool(struct mpipe_transform *self,
						     struct mpipe_dispatch *query)
{
	struct mpipe_vid_h264_encoder *enc = (struct mpipe_vid_h264_encoder *)self;

	/* A pool proposed downstream is used, the internal one otherwise */
	self->out_pool = (query->pool != NULL) ? query->pool : &enc->out_pool;

	return 0;
}

static int mpipe_vid_h264_encoder_set_property(struct mpipe_object *obj, uint32_t key,
					       const void *val)
{
	struct mpipe_vid_h264_encoder *enc = (struct mpipe_vid_h264_encoder *)obj;
	uint8_t value;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_VID_H264_ENCODER_QP:
		value = *(const uint8_t *)val;
		if (value < H264_ENCODER_QP_MIN || value > H264_ENCODER_QP_MAX) {
			return -EINVAL;
		}
		enc->qp = value;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_GOP:
		enc->gop = *(const uint16_t *)val;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_SPEED:
		value = *(const uint8_t *)val;
		if (value > H264_ENCODER_SPEED_MAX) {
			return -EINVAL;
		}
		enc->speed = value;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME:
		if (*(const bool *)val) {
			atomic_set(&enc->key_frame, 1);
		}
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mpipe_vid_h264_encoder_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_vid_h264_encoder *enc = (struct mpipe_vid_h264_encoder *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_VID_H264_ENCODER_QP:
		*(uint8_t *)val = enc->qp;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_GOP:
		*(uint16_t *)val = enc->gop;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_SPEED:
		*(uint8_t *)val = enc->speed;
		return 0;
	case MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME:
		*(bool *)val = atomic_get(&enc->key_frame) != 0;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mpipe_vid_h264_encoder_init(struct mpipe_vid_h264_encoder *enc, uint8_t id)
{
	__ASSERT_NO_MSG(enc != NULL);

	struct mpipe_element *self = &enc->transform.element;
	struct mpipe_transform *transform = &enc->transform;
	int ret = mpipe_transform_init(transform, id);

	if (ret != 0) {
		return ret;
	}

	h264_encoder_owner = enc;

	mpipe_element_set_name(self, "vid_h264_encoder");

	enc->qp = H264_ENCODER_QP_DEFAULT;
	enc->gop = H264_ENCODER_GOP_DEFAULT;
	enc->speed = H264_ENCODER_SPEED_DEFAULT;
	enc->width = 0;
	enc->height = 0;
	enc->ready = false;
	atomic_set(&enc->key_frame, 0);

	self->object.set_property = mpipe_vid_h264_encoder_set_property;
	self->object.get_property = mpipe_vid_h264_encoder_get_property;

	transform->mode = MPIPE_TRANSFORM_MODE_NORMAL;
	transform->out_pool = &enc->out_pool;

	transform->sink_pad.enum_caps_fn = mpipe_vid_h264_encoder_enum_caps;
	transform->src_pad.enum_caps_fn = mpipe_vid_h264_encoder_enum_caps;

	mpipe_vid_h264_encoder_out_pool_init(enc);

	transform->set_caps = mpipe_vid_h264_encoder_set_caps;
	transform->transform_caps = mpipe_vid_h264_encoder_transform_caps;
	transform->sink_pad.process_fn = mpipe_vid_h264_encoder_process_fn;
	transform->decide_buffer_pool = mpipe_vid_h264_encoder_decide_buffer_pool;

	return 0;
}

#define DT_DRV_COMPAT zephyr_mpipe_vid_h264_encoder

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
struct mpipe_vid_h264_encoder_dt_config {
	uint16_t gop;
	uint8_t qp;
	uint8_t speed;
};

static int mpipe_vid_h264_encoder_dt_init(const struct mpipe_dt_element *dt)
{
	const struct mpipe_vid_h264_encoder_dt_config *config = dt->config;
	int ret = mpipe_vid_h264_encoder_init((struct mpipe_vid_h264_encoder *)dt->element, dt->id);

	if (ret != 0) {
		return ret;
	}

	return mpipe_object_set_properties(&dt->element->object, MPIPE_PROP_VID_H264_ENCODER_QP,
					   &config->qp, MPIPE_PROP_VID_H264_ENCODER_GOP,
					   &config->gop, MPIPE_PROP_VID_H264_ENCODER_SPEED,
					   &config->speed, MPIPE_PROP_LIST_END);
}

#define MPIPE_VID_H264_ENCODER_DT_DEFINE(inst)                                                     \
	static struct mpipe_vid_h264_encoder mpipe_vid_h264_encoder_##inst;                        \
	static const struct mpipe_vid_h264_encoder_dt_config                                       \
		mpipe_vid_h264_encoder_dt_config_##inst = {                                        \
			.gop = DT_INST_PROP(inst, gop),                                            \
			.qp = DT_INST_PROP(inst, qp),                                              \
			.speed = DT_INST_PROP(inst, speed),                                        \
	};                                                                                         \
	MPIPE_DT_INST_ELEMENT_DEFINE(inst, &mpipe_vid_h264_encoder_##inst,                         \
				     mpipe_vid_h264_encoder_dt_init,                               \
				     &mpipe_vid_h264_encoder_dt_config_##inst);

DT_INST_FOREACH_STATUS_OKAY(MPIPE_VID_H264_ENCODER_DT_DEFINE)
#endif
