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

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_value.h>

#include <zephyr/mpipe/img/mpipe_img_jpeg_parser.h>

LOG_MODULE_REGISTER(mpipe_img_jpeg_parser, CONFIG_MPIPE_LOG_LEVEL);

/* Investigation aid, see mpipe_stage_stats.h */
#define MPIPE_STAGE_STATS IS_ENABLED(CONFIG_MPIPE_STAGE_STATS)
#if MPIPE_STAGE_STATS
#include "../mpipe_stage_stats.h"
static struct mpipe_stage_stats parse_stats;

/* One sample per chain call: frames cut, their bytes and the time the call took */
static void parse_stats_done(uint32_t t0, uint32_t frames, uint32_t bytes, uint32_t max_len)
{
	if (mpipe_stage_stats_add(&parse_stats, frames, bytes, max_len,
				  mpipe_stage_stats_elapsed_us(t0))) {
		LOG_INF("stats jpeg_parser: %u fr/s, avg %u max %u B, parse %u..%u us, %u ms "
			"total, drops %u (%u ms)",
			mpipe_stage_stats_per_s(&parse_stats, parse_stats.count),
			(parse_stats.count != 0U) ? parse_stats.bytes / parse_stats.count : 0U,
			parse_stats.bytes_max, parse_stats.us_min, parse_stats.us_max,
			parse_stats.us_sum / 1000U, parse_stats.aux, parse_stats.elapsed_ms);
		mpipe_stage_stats_reset(&parse_stats);
	}
}
#endif

/*
 * The parser only ever emits JPEG, so its source capability is known at build
 * time and lives in .rodata rather than being allocated at init.
 */
#define JPEG_PARSER_SRC_FIELDS(X) X(MPIPE_CAPS_PIXEL_FORMAT, MPIPE_VALUE_UINT(VIDEO_PIX_FMT_JPEG))

MPIPE_STRUCTURE_DEFINE(jpeg_parser_src_caps, MPIPE_MEDIA_VIDEO, JPEG_PARSER_SRC_FIELDS);

static int mpipe_img_jpeg_parser_enum_caps(struct mpipe_pad *pad, uint32_t index,
					   const struct mpipe_structure *filter,
					   struct mpipe_structure *out)
{
	/* The sink accepts anything, so the source is the only side to describe */
	if (pad->direction == MPIPE_PAD_SINK) {
		if (index > 0) {
			return -ENOENT;
		}

		if (filter != NULL) {
			*out = *filter;
			return 0;
		}

		return mpipe_structure_init_any(out);
	}

	if (index > 0) {
		return -ENOENT;
	}

	return mpipe_pad_enum_filter(&jpeg_parser_src_caps, filter, out);
}

/*
 * Internal pool for the frames that have to be copied: those split across two
 * input chunks when no downstream pool was adopted. A frame lying inside one
 * chunk is emitted as a view of it and needs no buffer here.
 */
NET_BUF_POOL_FIXED_DEFINE(mpipe_img_jpeg_parser_pool, CONFIG_MPIPE_IMG_JPEG_PARSER_POOL_NUM,
			  CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE,
			  sizeof(struct mpipe_buffer_meta), mpipe_buffer_destroy);

#define JPEG_EOI_BYTE0 0xFFU
#define JPEG_EOI_BYTE1 0xD9U

static int mpipe_img_jpeg_parser_decide_buffer_pool(struct mpipe_parser *parser,
						    struct mpipe_dispatch *query)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)parser;

	parser->out_pool = NULL;
	jpeg_parser->copy_out = false;

	if (CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE > 0) {
		parser->out_pool = &jpeg_parser->out_pool;
	}

	/*
	 * An element downstream that offers a pool needs its frames in that
	 * memory, so every frame is copied into it. The parser holds no frame
	 * of its own across chain calls, so it adds no demand to the pool.
	 */
	if (query->pool != NULL) {
		parser->out_pool = query->pool;
		jpeg_parser->copy_out = true;
	}

	if (parser->out_pool == NULL) {
		LOG_ERR("No pool to copy a frame into; set "
			"CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE");
		return -ENOMEM;
	}

	return 0;
}

static inline void set_bytes_used(struct net_buf *buf, uint32_t used)
{
	struct mpipe_buffer_meta *bm = mpipe_buffer_get_meta(buf);

	bm->bytes_used = used;
	buf->len = used;

	/* Keep the video buffer, if the pool wraps one, in sync for the driver */
	if (bm->driver_buf != NULL) {
		((struct video_buffer *)bm->driver_buf)->bytesused = used;
	}
}

static inline uint32_t piece_len(const struct net_buf *piece)
{
	return mpipe_buffer_get_meta(piece)->bytes_used;
}

/* Drop the first @p n bytes of a pending piece, whose memory stays valid */
static inline void piece_pull(struct net_buf *piece, uint32_t n)
{
	struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(piece);

	piece->data += n;
	m->bytes_used -= n;
	piece->len = m->bytes_used;
}

static int mpipe_img_jpeg_parser_release_buffer(struct mpipe_buffer_pool *pool, struct net_buf *buf)
{
	struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(buf);

	ARG_UNUSED(pool);

	m->bytes_used = 0;
	m->timestamp = 0;
	m->driver_buf = NULL;
	m->priv = NULL;
	buf->len = 0;

	return 0;
}

static int mpipe_img_jpeg_parser_acquire_buffer(struct mpipe_buffer_pool *pool,
						struct net_buf **buf)
{
	struct mpipe_buffer_meta *m;
	struct net_buf *out;

	out = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_FOREVER);
	if (out == NULL) {
		return -ENOBUFS;
	}

	m = mpipe_buffer_get_meta(out);
	m->pool = pool;
	m->bytes_used = 0;
	m->timestamp = 0;
	m->driver_buf = NULL;
	m->priv = NULL;
	m->parent = NULL;
	out->len = 0;

	*buf = out;

	return 0;
}

/*
 * Add an input to the pending data. A view that continues the last pending
 * piece in the same chunk extends that piece instead, so the pending chain
 * holds one piece per chunk however small the reads are.
 */
static void mpipe_img_jpeg_parser_append(struct mpipe_img_jpeg_parser *jpeg_parser,
					 struct net_buf *in_buf)
{
	struct net_buf *tail = jpeg_parser->pending;
	const struct mpipe_buffer_meta *in_meta = mpipe_buffer_get_meta(in_buf);
	struct mpipe_buffer_meta *tail_meta;

	in_buf->frags = NULL;

	if (tail == NULL) {
		jpeg_parser->pending = in_buf;
		return;
	}

	while (tail->frags != NULL) {
		tail = tail->frags;
	}

	tail_meta = mpipe_buffer_get_meta(tail);
	if (in_meta->parent != NULL && in_meta->parent == tail_meta->parent &&
	    in_buf->data == tail->data + tail_meta->bytes_used) {
		tail_meta->bytes_used += in_meta->bytes_used;
		tail->len = tail_meta->bytes_used;
		tail->size = tail->len;
		net_buf_unref(in_buf);
		return;
	}

	tail->frags = in_buf;
}

static uint32_t mpipe_img_jpeg_parser_pending_len(const struct mpipe_img_jpeg_parser *jpeg_parser)
{
	uint32_t total = 0;

	for (const struct net_buf *p = jpeg_parser->pending; p != NULL; p = p->frags) {
		total += piece_len(p);
	}

	return total;
}

/*
 * Find the end-of-image marker in the pending data, searching from @p from,
 * which may sit on the second marker byte so that a marker split across two
 * calls is found. Returns the offset of the byte after the marker, or 0 when
 * there is none.
 */
static uint32_t mpipe_img_jpeg_parser_find_eoi(const struct mpipe_img_jpeg_parser *jpeg_parser,
					       uint32_t from)
{
	uint32_t offset = 0;
	uint8_t prev = 0;

	for (const struct net_buf *p = jpeg_parser->pending; p != NULL; p = p->frags) {
		uint32_t len = piece_len(p);
		uint32_t start = 0;

		if (offset + len <= from) {
			offset += len;
			if (len > 0U) {
				prev = p->data[len - 1U];
			}
			continue;
		}

		if (from > offset) {
			start = from - offset;
			prev = p->data[start - 1U];
		}

		for (uint32_t i = start; i < len; i++) {
			uint8_t b = p->data[i];

			if (prev == JPEG_EOI_BYTE0 && b == JPEG_EOI_BYTE1) {
				return offset + i + 1U;
			}
			prev = b;
		}

		offset += len;
	}

	return 0;
}

/* Release the first @p n bytes of the pending data */
static void mpipe_img_jpeg_parser_consume(struct mpipe_img_jpeg_parser *jpeg_parser, uint32_t n)
{
	while (n > 0U && jpeg_parser->pending != NULL) {
		struct net_buf *p = jpeg_parser->pending;
		uint32_t len = piece_len(p);

		if (n < len) {
			piece_pull(p, n);
			return;
		}

		n -= len;
		jpeg_parser->pending = p->frags;
		p->frags = NULL;
		net_buf_unref(p);
	}
}

/* Copy the first @p len pending bytes into @p out */
static void mpipe_img_jpeg_parser_copy_out(const struct mpipe_img_jpeg_parser *jpeg_parser,
					   struct net_buf *out, uint32_t len)
{
	uint32_t done = 0;

	for (const struct net_buf *p = jpeg_parser->pending; p != NULL && done < len;
	     p = p->frags) {
		uint32_t n = MIN(piece_len(p), len - done);

		memcpy(out->data + done, p->data, n);
		done += n;
	}

	set_bytes_used(out, len);
}

/*
 * Produce the frame made of the first @p len pending bytes: a view of the
 * chunk when the frame sits inside one piece and nothing downstream needs
 * its own memory, a copy into the output pool otherwise.
 */
static struct net_buf *mpipe_img_jpeg_parser_cut(struct mpipe_img_jpeg_parser *jpeg_parser,
						 uint32_t len)
{
	struct mpipe_buffer_pool *out_pool = jpeg_parser->base.out_pool;
	struct net_buf *first = jpeg_parser->pending;
	struct net_buf *out = NULL;

	if (!jpeg_parser->copy_out && len <= piece_len(first)) {
		struct net_buf *parent = mpipe_buffer_get_meta(first)->parent;
		uint32_t offset = 0;

		if (parent == NULL) {
			parent = first;
		} else {
			offset = (uint32_t)(first->data - parent->data);
		}

		out = mpipe_buffer_view(parent, offset, len, K_NO_WAIT);
		if (out != NULL) {
			return out;
		}

		LOG_WRN("No view header left, copying the frame");
	}

	if (out_pool->acquire_buffer(out_pool, &out) != 0 || out == NULL) {
		LOG_ERR("Failed to acquire an output buffer");
		return NULL;
	}

	if (len > mpipe_buffer_get_meta(out)->pool->config.size) {
		LOG_WRN("Dropping a %u-byte JPEG frame, the output buffers hold %u", len,
			mpipe_buffer_get_meta(out)->pool->config.size);
		net_buf_unref(out);
		return NULL;
	}

	mpipe_img_jpeg_parser_copy_out(jpeg_parser, out, len);

	return out;
}

/*
 * The stream is a sequence of concatenated JPEG frames, so the end marker
 * alone delimits them: the pending data starts on a frame start, and each
 * frame that follows starts on the byte after the previous end marker.
 * Matching the start marker as well would buy nothing and cost a second scan
 * of every byte. Only the bytes that arrived since the last call are scanned.
 */
static int mpipe_img_jpeg_parser_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					    struct net_buf **out_buf)
{
	struct mpipe_img_jpeg_parser *jpeg_parser =
		(struct mpipe_img_jpeg_parser *)pad->object.container;
	uint32_t limit = jpeg_parser->base.out_pool->config.size;
	uint32_t pending_len;
#if MPIPE_STAGE_STATS
	uint32_t t0 = k_cycle_get_32();
	uint32_t st_frames = 0;
	uint32_t st_bytes = 0;
	uint32_t st_max = 0;
#endif

	*out_buf = NULL;

	mpipe_img_jpeg_parser_append(jpeg_parser, in_buf);

	while (true) {
		uint32_t end = mpipe_img_jpeg_parser_find_eoi(jpeg_parser, jpeg_parser->scanned);
		struct net_buf *frame;

		if (end == 0U) {
			break;
		}

		frame = mpipe_img_jpeg_parser_cut(jpeg_parser, end);
		if (frame != NULL) {
			if (*out_buf == NULL) {
				*out_buf = frame;
			} else {
				net_buf_frag_add(*out_buf, frame);
			}
#if MPIPE_STAGE_STATS
			st_frames++;
			st_bytes += end;
			st_max = MAX(st_max, end);
#endif
		}

		mpipe_img_jpeg_parser_consume(jpeg_parser, end);
		jpeg_parser->scanned = 0;
	}

	/*
	 * The remaining bytes are the head of the next frame. They stay where
	 * they are for the next call, which resumes the scan one byte back so
	 * that a marker split across two calls is still found.
	 */
	pending_len = mpipe_img_jpeg_parser_pending_len(jpeg_parser);
	jpeg_parser->scanned = (pending_len > 0U) ? pending_len - 1U : 0U;

	/*
	 * A frame longer than the output buffers can never be produced, and
	 * holding its bytes would pin the source's chunks for ever. Drop them
	 * and resume on the bytes that follow.
	 */
	if (pending_len > limit) {
		LOG_WRN("Dropping %u bytes without an end marker, more than a %u-byte frame",
			pending_len, limit);
		mpipe_img_jpeg_parser_consume(jpeg_parser, pending_len);
		jpeg_parser->scanned = 0;
#if MPIPE_STAGE_STATS
		parse_stats.aux++;
#endif
	}

#if MPIPE_STAGE_STATS
	parse_stats_done(t0, st_frames, st_bytes, st_max);
#endif

	return 0;
}

static int mpipe_img_jpeg_parser_set_property(struct mpipe_object *obj, uint32_t key,
					      const void *val)
{
	ARG_UNUSED(obj);
	ARG_UNUSED(val);

	LOG_ERR("Property %u is unknown", key);

	return -ENOTSUP;
}

static int mpipe_img_jpeg_parser_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	ARG_UNUSED(obj);
	ARG_UNUSED(val);

	LOG_ERR("Property %u is unknown", key);

	return -ENOTSUP;
}

static int mpipe_img_jpeg_parser_change_state(struct mpipe_element *self,
					      enum mpipe_state_change transition)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)self;

	/*
	 * On teardown, drop the head of the frame that never completed, or
	 * the next stream's opening bytes would be spliced onto it.
	 */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		if (jpeg_parser->pending != NULL) {
			net_buf_unref(jpeg_parser->pending);
			jpeg_parser->pending = NULL;
		}
		jpeg_parser->scanned = 0;
	}

	/* The base resets the negotiated pad caps and stops the output pool */
	return mpipe_parser_change_state(self, transition);
}

int mpipe_img_jpeg_parser_init(struct mpipe_img_jpeg_parser *jpeg_parser, uint8_t id)
{
	__ASSERT_NO_MSG(jpeg_parser != NULL);

	struct mpipe_element *self = &jpeg_parser->base.element;
	struct mpipe_parser *parser = &jpeg_parser->base;
	int ret = mpipe_parser_init(parser, id);

	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(self, "img_jpeg_parser");

	self->change_state = mpipe_img_jpeg_parser_change_state;
	self->object.set_property = mpipe_img_jpeg_parser_set_property;
	self->object.get_property = mpipe_img_jpeg_parser_get_property;

	jpeg_parser->pending = NULL;
	jpeg_parser->scanned = 0;
	jpeg_parser->copy_out = false;

	/*
	 * The capabilities are enumerated from .rodata, so the ANY caps that
	 * mpipe_parser_init() leaves on both sides are never consulted.
	 */
	parser->sink_pad.enum_caps_fn = mpipe_img_jpeg_parser_enum_caps;
	parser->src_pad.enum_caps_fn = mpipe_img_jpeg_parser_enum_caps;

	parser->sink_pad.process_fn = mpipe_img_jpeg_parser_process_fn;
	parser->decide_buffer_pool = mpipe_img_jpeg_parser_decide_buffer_pool;

	if (CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE > 0) {
		const struct mpipe_buffer_pool_config pool_req = {
			.size = CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE,
		};

		mpipe_buffer_pool_init(&jpeg_parser->out_pool);
		jpeg_parser->out_pool.nb_pool = &mpipe_img_jpeg_parser_pool;
		(void)mpipe_buffer_pool_set_req_config(&jpeg_parser->out_pool, &pool_req);
		jpeg_parser->out_pool.acquire_buffer = mpipe_img_jpeg_parser_acquire_buffer;
		jpeg_parser->out_pool.release_buffer = mpipe_img_jpeg_parser_release_buffer;
		parser->out_pool = &jpeg_parser->out_pool;
	}

	return 0;
}
