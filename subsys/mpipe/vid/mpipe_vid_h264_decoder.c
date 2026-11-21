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
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>

#include <H264SwDecApi.h>

/* Investigation aid, see mpipe_stage_stats.h */
#define MPIPE_STAGE_STATS IS_ENABLED(CONFIG_MPIPE_STAGE_STATS)
#if MPIPE_STAGE_STATS
#include "../mpipe_stage_stats.h"
static struct mpipe_stage_stats dec_stats;
#endif

LOG_MODULE_REGISTER(mpipe_vid_h264_decoder, CONFIG_MPIPE_LOG_LEVEL);

#define H264_START_CODE_SIZE 3U
#define H264_NAL_TYPE_MASK   0x1FU
#define H264_NAL_SLICE       1U
#define H264_NAL_IDR         5U
#define H264_NAL_SPS         7U
#define H264_NAL_PPS         8U

/*
 * The decoder stops at every picture and every parameter set, so one call
 * per NAL unit and a few more is the most a buffer needs.
 */
#define H264_DECODER_MAX_STEPS 32U

/* A key frame request that got no answer is made again after this many slices */
#define H264_DECODER_KEY_FRAME_RETRY 30U

/* Internal output pool (used when downstream doesn't propose a pool) */
NET_BUF_POOL_FIXED_DEFINE(mpipe_vid_h264_dec_pool, CONFIG_MPIPE_VID_H264_DECODER_POOL_NUM,
			  CONFIG_MPIPE_VID_H264_DECODER_MAX_OUT_FRAME_SIZE,
			  sizeof(struct mpipe_buffer_meta), mpipe_buffer_destroy);

/* What a buffer of the byte stream holds */
struct h264_scan {
	/* Offset and size of the last parameter sets, start code included */
	uint32_t sps_offset;
	uint32_t sps_size;
	uint32_t pps_offset;
	uint32_t pps_size;
	/* A slice of a key frame */
	bool idr;
	/* A slice of another frame */
	bool slice;
};

static int mpipe_vid_h264_decoder_out_pool_acquire(struct mpipe_buffer_pool *pool,
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

static int mpipe_vid_h264_decoder_out_pool_release(struct mpipe_buffer_pool *pool,
						   struct net_buf *buf)
{
	ARG_UNUSED(pool);
	ARG_UNUSED(buf);

	return 0;
}

static void mpipe_vid_h264_decoder_out_pool_init(struct mpipe_vid_h264_decoder *dec)
{
	const struct mpipe_buffer_pool_config pool_req = {
		.size = CONFIG_MPIPE_VID_H264_DECODER_MAX_OUT_FRAME_SIZE,
		.align = 1,
		.min_buffers = CONFIG_MPIPE_VID_H264_DECODER_POOL_NUM,
		.max_buffers = CONFIG_MPIPE_VID_H264_DECODER_POOL_NUM,
	};

	mpipe_buffer_pool_init(&dec->out_pool);
	dec->out_pool.nb_pool = &mpipe_vid_h264_dec_pool;
	(void)mpipe_buffer_pool_set_req_config(&dec->out_pool, &pool_req);
	dec->out_pool.acquire_buffer = mpipe_vid_h264_decoder_out_pool_acquire;
	dec->out_pool.release_buffer = mpipe_vid_h264_decoder_out_pool_release;
	dec->out_pool.started = true; /* net_buf pool is static; no explicit start */
}

/* Offset of the next start code at or after @p from, @p size when there is none */
static uint32_t h264_find_start_code(const uint8_t *data, uint32_t size, uint32_t from)
{
	for (uint32_t i = from; i + H264_START_CODE_SIZE <= size; i++) {
		if (data[i] == 0U && data[i + 1U] == 0U && data[i + 2U] == 1U) {
			return i;
		}
	}

	return size;
}

static void h264_scan_buffer(const uint8_t *data, uint32_t size, struct h264_scan *scan)
{
	uint32_t start = h264_find_start_code(data, size, 0U);

	*scan = (struct h264_scan){0};

	while (start + H264_START_CODE_SIZE < size) {
		uint32_t next = h264_find_start_code(data, size, start + H264_START_CODE_SIZE);
		uint8_t type = data[start + H264_START_CODE_SIZE] & H264_NAL_TYPE_MASK;

		if (type == H264_NAL_IDR) {
			scan->idr = true;
		} else if (type == H264_NAL_SLICE) {
			scan->slice = true;
		} else if (type == H264_NAL_SPS) {
			scan->sps_offset = start;
			scan->sps_size = next - start;
		} else if (type == H264_NAL_PPS) {
			scan->pps_offset = start;
			scan->pps_size = next - start;
		} else {
			/* Nothing to remember about the other NAL unit types */
		}

		start = next;
	}
}

/* Keep the parameter sets of a buffer for the decoder that follows a stream error */
static void mpipe_vid_h264_decoder_keep_param_sets(struct mpipe_vid_h264_decoder *dec,
						   const uint8_t *data,
						   const struct h264_scan *scan)
{
	if (scan->sps_size > 0U && scan->sps_size <= sizeof(dec->sps)) {
		memcpy(dec->sps, &data[scan->sps_offset], scan->sps_size);
		dec->sps_size = (uint16_t)scan->sps_size;
	}

	if (scan->pps_size > 0U && scan->pps_size <= sizeof(dec->pps)) {
		memcpy(dec->pps, &data[scan->pps_offset], scan->pps_size);
		dec->pps_size = (uint16_t)scan->pps_size;
	}
}

static int mpipe_vid_h264_decoder_create(struct mpipe_vid_h264_decoder *dec)
{
	H264SwDecInst inst = NULL;
	H264SwDecRet res;

	if (dec->inst != NULL) {
		H264SwDecRelease((H264SwDecInst)dec->inst);
		dec->inst = NULL;
	}

	/* Pictures leave in decoding order: no reordering delay */
	res = H264SwDecInit(&inst, 1U);
	if (res != H264SWDEC_OK) {
		LOG_ERR("Decoder creation failed (%d)", (int)res);
		return (res == H264SWDEC_MEMFAIL) ? -ENOMEM : -EIO;
	}

	dec->inst = inst;
	dec->waiting_for_key_frame = true;
	/* A stream joined in its middle: the first slice asks for the key frame */
	dec->slices_to_request = 1U;
	dec->size_reported = false;

	return 0;
}

/* Copy the visible part of a decoded picture into an output buffer */
static int mpipe_vid_h264_decoder_copy_picture(struct mpipe_vid_h264_decoder *dec,
					       const uint8_t *picture, struct net_buf *out)
{
	H264SwDecInfo info;
	uint32_t left = 0U;
	uint32_t top = 0U;
	uint32_t width;
	uint32_t height;
	uint32_t size;
	const uint8_t *src;
	uint8_t *dst = out->data;

	if (H264SwDecGetInfo((H264SwDecInst)dec->inst, &info) != H264SWDEC_OK) {
		return -EBADMSG;
	}

	width = info.picWidth;
	height = info.picHeight;
	if (info.croppingFlag != 0U) {
		left = info.cropParams.cropLeftOffset;
		top = info.cropParams.cropTopOffset;
		width = info.cropParams.cropOutWidth;
		height = info.cropParams.cropOutHeight;
	}

	size = width * height * 3U / 2U;

	if ((dec->width != 0U && (width != dec->width || height != dec->height)) ||
	    size > out->size) {
		if (!dec->size_reported) {
			LOG_WRN("The stream holds %ux%u pictures, %ux%u was negotiated and the "
				"output buffer holds %u bytes",
				width, height, dec->width, dec->height, out->size);
			dec->size_reported = true;
		}
		return -EMSGSIZE;
	}

	if (info.croppingFlag == 0U) {
		memcpy(dst, picture, size);
	} else {
		/* Luma rows, then the rows of each chroma plane at half the size */
		src = picture + (top * info.picWidth) + left;
		for (uint32_t row = 0U; row < height; row++) {
			memcpy(dst, src, width);
			dst += width;
			src += info.picWidth;
		}

		for (uint32_t plane = 0U; plane < 2U; plane++) {
			src = picture + (info.picWidth * info.picHeight) +
			      (plane * (info.picWidth / 2U) * (info.picHeight / 2U)) +
			      ((top / 2U) * (info.picWidth / 2U)) + (left / 2U);
			for (uint32_t row = 0U; row < height / 2U; row++) {
				memcpy(dst, src, width / 2U);
				dst += width / 2U;
				src += info.picWidth / 2U;
			}
		}
	}

	mpipe_buffer_get_meta(out)->bytes_used = size;
	out->len = size;

	return 0;
}

/*
 * Give a piece of the byte stream to the decoder. A decoded picture is copied
 * into @p out, when there is one; @p got_picture tells whether @p out holds a
 * picture.
 */
static int mpipe_vid_h264_decoder_feed(struct mpipe_vid_h264_decoder *dec, uint8_t *data,
				       uint32_t size, struct net_buf *out, bool *got_picture)
{
	H264SwDecInput input = {
		.pStream = data,
		.dataLen = size,
		.picId = dec->pic_id,
	};
	H264SwDecOutput output = {0};
	H264SwDecPicture picture;
	H264SwDecRet res;
	uint32_t consumed;

	for (uint32_t step = 0U; step < H264_DECODER_MAX_STEPS && input.dataLen > 0U; step++) {
		res = H264SwDecDecode((H264SwDecInst)dec->inst, &input, &output);
		if (res == H264SWDEC_MEMFAIL) {
			/* Not the stream: the heap of the decoder library is too small for it */
			LOG_ERR("The decoder is out of memory");
			return -ENOMEM;
		}

		if ((int)res < 0) {
			LOG_DBG("Decoding failed (%d)", (int)res);
			return -EBADMSG;
		}

		if (output.pStrmCurrPos == NULL || output.pStrmCurrPos < input.pStream ||
		    output.pStrmCurrPos > input.pStream + input.dataLen) {
			return -EBADMSG;
		}

		consumed = (uint32_t)(output.pStrmCurrPos - input.pStream);
		input.pStream = output.pStrmCurrPos;
		input.dataLen -= consumed;

		/* Parameter sets or part of a picture: go on with what is left */
		if (res != H264SWDEC_PIC_RDY && res != H264SWDEC_PIC_RDY_BUFF_NOT_EMPTY) {
			if (consumed == 0U && res != H264SWDEC_HDRS_RDY_BUFF_NOT_EMPTY) {
				/* The decoder makes no progress on what is left */
				return -EBADMSG;
			}

			continue;
		}

		dec->pic_id++;
		input.picId = dec->pic_id;

		while (H264SwDecNextPicture((H264SwDecInst)dec->inst, &picture, 0U) ==
		       H264SWDEC_PIC_RDY) {
			if (out == NULL || picture.pOutputPicture == NULL) {
				continue;
			}

			if (mpipe_vid_h264_decoder_copy_picture(
				    dec, (const uint8_t *)picture.pOutputPicture, out) == 0) {
				*got_picture = true;
			}
		}
	}

	return (input.dataLen == 0U) ? 0 : -EBADMSG;
}

static void mpipe_vid_h264_decoder_request_key_frame(struct mpipe_vid_h264_decoder *dec)
{
	if (dec->key_frame_cb.fn != NULL) {
		dec->key_frame_cb.fn(dec, dec->key_frame_cb.user_data);
	}
}

/* A stream error: start a new decoder and ask for a key frame */
static void mpipe_vid_h264_decoder_restart(struct mpipe_vid_h264_decoder *dec)
{
	LOG_WRN("Stream error, waiting for a key frame");

	(void)mpipe_vid_h264_decoder_create(dec);
	mpipe_vid_h264_decoder_request_key_frame(dec);
	dec->slices_to_request = H264_DECODER_KEY_FRAME_RETRY;
}

/* A slice that cannot be decoded: the key frame it waits for may have to be asked for */
static void mpipe_vid_h264_decoder_discard_slice(struct mpipe_vid_h264_decoder *dec)
{
	dec->slices_to_request--;
	if (dec->slices_to_request == 0U) {
		LOG_DBG("Still no key frame, asking for one");
		mpipe_vid_h264_decoder_request_key_frame(dec);
		dec->slices_to_request = H264_DECODER_KEY_FRAME_RETRY;
	}
}

static int mpipe_vid_h264_decoder_decode_one(struct mpipe_vid_h264_decoder *dec,
					     struct net_buf *in_buf, struct net_buf *out_buf,
					     bool *got_picture)
{
	uint32_t in_size = mpipe_buffer_get_meta(in_buf)->bytes_used;
	struct h264_scan scan;
	bool unused = false;
	int ret = 0;

	h264_scan_buffer(in_buf->data, in_size, &scan);

	if (dec->waiting_for_key_frame) {
		if (!scan.idr) {
			/* Not decodable on its own; its parameter sets serve the key frame */
			mpipe_vid_h264_decoder_keep_param_sets(dec, in_buf->data, &scan);
			if (scan.slice) {
				mpipe_vid_h264_decoder_discard_slice(dec);
			}

			return 0;
		}

		/* The new decoder has not seen the parameter sets of the stream */
		if (scan.sps_size == 0U && dec->sps_size > 0U) {
			ret = mpipe_vid_h264_decoder_feed(dec, dec->sps, dec->sps_size, NULL,
							  &unused);
		}

		if (ret == 0 && scan.pps_size == 0U && dec->pps_size > 0U) {
			ret = mpipe_vid_h264_decoder_feed(dec, dec->pps, dec->pps_size, NULL,
							  &unused);
		}
	}

	if (ret == 0) {
		ret = mpipe_vid_h264_decoder_feed(dec, in_buf->data, in_size, out_buf, got_picture);
	}

	if (ret != 0) {
		mpipe_vid_h264_decoder_restart(dec);
		return ret;
	}

	if (scan.idr) {
		dec->waiting_for_key_frame = false;
	}

	mpipe_vid_h264_decoder_keep_param_sets(dec, in_buf->data, &scan);

	return 0;
}

static int mpipe_vid_h264_decoder_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					     struct net_buf **out_buf)
{
	struct mpipe_transform *transform = (struct mpipe_transform *)pad->object.container;
	struct mpipe_vid_h264_decoder *dec = (struct mpipe_vid_h264_decoder *)transform;
	struct mpipe_buffer_pool *out_pool = transform->out_pool;
	struct net_buf *out = NULL;
	bool got_picture = false;
	int ret;

	*out_buf = NULL;

	if (out_pool == NULL) {
		out_pool = &dec->out_pool;
	}

	if (in_buf == NULL || out_pool->acquire_buffer == NULL) {
		return -EINVAL;
	}

	if (dec->inst == NULL) {
		LOG_ERR("Missing negotiated caps");
		net_buf_unref(in_buf);
		return -EINVAL;
	}

	/*
	 * Without an output buffer the picture is still decoded, since the
	 * pictures after it refer to it; it is only not shown.
	 */
	ret = out_pool->acquire_buffer(out_pool, &out);
	if (ret != 0) {
		out = NULL;
	}

#if MPIPE_STAGE_STATS
	uint32_t t0 = k_cycle_get_32();
	uint32_t in_size = mpipe_buffer_get_meta(in_buf)->bytes_used;
#endif

	ret = mpipe_vid_h264_decoder_decode_one(dec, in_buf, out, &got_picture);

#if MPIPE_STAGE_STATS
	if (ret != 0) {
		dec_stats.aux++;
	}
	if (mpipe_stage_stats_add(&dec_stats, got_picture ? 1U : 0U, in_size, in_size,
				  mpipe_stage_stats_elapsed_us(t0))) {
		LOG_INF("stats h264_decoder: %u fr/s, in %u B/s, "
			"decode %u/%u/%u us min/avg/max per buffer, fail %u (%u ms)",
			mpipe_stage_stats_per_s(&dec_stats, dec_stats.count),
			mpipe_stage_stats_per_s(&dec_stats, dec_stats.bytes), dec_stats.us_min,
			mpipe_stage_stats_us_avg(&dec_stats), dec_stats.us_max, dec_stats.aux,
			dec_stats.elapsed_ms);
		mpipe_stage_stats_reset(&dec_stats);
	}
#endif

	if (got_picture) {
		mpipe_buffer_get_meta(out)->pts = mpipe_buffer_get_meta(in_buf)->pts;
		*out_buf = out;
	} else if (out != NULL) {
		net_buf_unref(out);
	} else {
		/* Nothing was acquired and nothing is to be pushed */
	}

	net_buf_unref(in_buf);

	/* A stream error costs the frames up to the next key frame, not the stream */
	return 0;
}

static int mpipe_vid_h264_decoder_candidate(uint32_t pixfmt, struct mpipe_structure *out)
{
	return mpipe_structure_init_fields(out, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					   MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_END);
}

static int mpipe_vid_h264_decoder_enum_caps(struct mpipe_pad *pad, uint32_t index,
					    const struct mpipe_structure *filter,
					    struct mpipe_structure *out)
{
	struct mpipe_structure candidate;
	int ret;

	if (index > 0U) {
		return -ENOENT;
	}

	ret = mpipe_vid_h264_decoder_candidate(
		(pad->direction == MPIPE_PAD_SINK) ? VIDEO_PIX_FMT_H264 : VIDEO_PIX_FMT_YUV420,
		&candidate);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int mpipe_vid_h264_decoder_transform_caps(struct mpipe_transform *transform,
						 enum mpipe_pad_direction direction,
						 const struct mpipe_structure *in, uint32_t index,
						 struct mpipe_structure *out)
{
	/* Decoding changes the format, not the geometry or the timing */
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

	ret = mpipe_vid_h264_decoder_candidate(
		(direction == MPIPE_PAD_SRC) ? VIDEO_PIX_FMT_YUV420 : VIDEO_PIX_FMT_H264, out);
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

static int mpipe_vid_h264_decoder_set_caps(struct mpipe_transform *transform,
					   enum mpipe_pad_direction direction,
					   const struct mpipe_structure *caps)
{
	struct mpipe_vid_h264_decoder *dec = (struct mpipe_vid_h264_decoder *)transform;
	const struct mpipe_value *v;
	int ret;

	if (caps == NULL) {
		return -EINVAL;
	}

	ret = mpipe_transform_set_caps(transform, direction, caps);
	if (ret < 0 || direction != MPIPE_PAD_SINK) {
		return ret;
	}

	dec->width = 0;
	dec->height = 0;

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_WIDTH);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		dec->width = (uint16_t)mpipe_value_get_uint(v);
	}

	v = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_HEIGHT);
	if (v != NULL && v->type == MPIPE_TYPE_UINT) {
		dec->height = (uint16_t)mpipe_value_get_uint(v);
	}

	/* A new stream: nothing of the previous one is kept */
	dec->sps_size = 0;
	dec->pps_size = 0;
	dec->pic_id = 0;

	return mpipe_vid_h264_decoder_create(dec);
}

static int mpipe_vid_h264_decoder_decide_buffer_pool(struct mpipe_transform *self,
						     struct mpipe_dispatch *query)
{
	struct mpipe_vid_h264_decoder *dec = (struct mpipe_vid_h264_decoder *)self;

	/* A pool proposed downstream is used, the internal one otherwise */
	self->out_pool = (query->pool != NULL) ? query->pool : &dec->out_pool;

	return 0;
}

static int mpipe_vid_h264_decoder_set_property(struct mpipe_object *obj, uint32_t key,
					       const void *val)
{
	struct mpipe_vid_h264_decoder *dec = (struct mpipe_vid_h264_decoder *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB:
		dec->key_frame_cb = *(const struct mpipe_vid_h264_decoder_cb *)val;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mpipe_vid_h264_decoder_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_vid_h264_decoder *dec = (struct mpipe_vid_h264_decoder *)obj;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (key) {
	case MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB:
		*(struct mpipe_vid_h264_decoder_cb *)val = dec->key_frame_cb;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mpipe_vid_h264_decoder_init(struct mpipe_vid_h264_decoder *dec, uint8_t id)
{
	__ASSERT_NO_MSG(dec != NULL);

	struct mpipe_element *self = &dec->transform.element;
	struct mpipe_transform *transform = &dec->transform;
	int ret = mpipe_transform_init(transform, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "vid_h264_decoder");

	dec->key_frame_cb = (struct mpipe_vid_h264_decoder_cb){0};
	dec->inst = NULL;
	dec->pic_id = 0;
	dec->width = 0;
	dec->height = 0;
	dec->sps_size = 0;
	dec->pps_size = 0;
	dec->slices_to_request = 1U;
	dec->waiting_for_key_frame = true;
	dec->size_reported = false;

	self->object.set_property = mpipe_vid_h264_decoder_set_property;
	self->object.get_property = mpipe_vid_h264_decoder_get_property;

	transform->mode = MPIPE_TRANSFORM_MODE_NORMAL;
	transform->out_pool = &dec->out_pool;

	transform->sink_pad.enum_caps_fn = mpipe_vid_h264_decoder_enum_caps;
	transform->src_pad.enum_caps_fn = mpipe_vid_h264_decoder_enum_caps;

	mpipe_vid_h264_decoder_out_pool_init(dec);

	transform->set_caps = mpipe_vid_h264_decoder_set_caps;
	transform->transform_caps = mpipe_vid_h264_decoder_transform_caps;
	transform->sink_pad.process_fn = mpipe_vid_h264_decoder_process_fn;
	transform->decide_buffer_pool = mpipe_vid_h264_decoder_decide_buffer_pool;

	return 0;
}

#define DT_DRV_COMPAT zephyr_mpipe_vid_h264_decoder

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int mpipe_vid_h264_decoder_dt_init(const struct mpipe_dt_element *dt)
{
	return mpipe_vid_h264_decoder_init((struct mpipe_vid_h264_decoder *)dt->element, dt->id);
}

#define MPIPE_VID_H264_DECODER_DT_DEFINE(inst)                                                     \
	static struct mpipe_vid_h264_decoder mpipe_vid_h264_decoder_##inst;                        \
	MPIPE_DT_INST_ELEMENT_DEFINE(inst, &mpipe_vid_h264_decoder_##inst,                         \
				     mpipe_vid_h264_decoder_dt_init, NULL);

DT_INST_FOREACH_STATUS_OKAY(MPIPE_VID_H264_DECODER_DT_DEFINE)
#endif
