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
 * Internal output pool which is static and simple. For specific requirements, e.g. alignment,
 * the proposed downstream pool will be used
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
	struct mpipe_buffer_pool *query_pool = query->pool;

	/* Use the internal pool by default */
	if (CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE > 0) {
		parser->out_pool = &jpeg_parser->out_pool;
	}

	/* Use the proposed pool from downstream when available */
	if (query_pool != NULL) {
		struct mpipe_buffer_pool_config cfg = query_pool->config;
		int ret;

		/* Add one extra buffer to hold the parser's partial frame */
		cfg.min_buffers += 1;

		/*
		 * The demand reaches the pool only through the owner-validated
		 * setter, which may refuse it.
		 */
		ret = mpipe_buffer_pool_set_config(query_pool, &cfg);
		if (ret == 0) {
			parser->out_pool = query_pool;
		} else {
			LOG_WRN("Proposed pool refused the config (%d), keeping the internal pool",
				ret);
		}
	}

	if (parser->out_pool == NULL) {
		return -ENOMEM;
	}

	/* TODO: Do negotiation when downstream only propose pool config */

	return 0;
}

static uint8_t *find_jpeg_eoi(const uint8_t *data, size_t len)
{
	if (data == NULL || len < 2) {
		return NULL;
	}

	for (size_t i = 0; i < len - 1; i++) {
		if (data[i] == JPEG_EOI_BYTE0 && data[i + 1] == JPEG_EOI_BYTE1) {
			return (uint8_t *)&data[i];
		}
	}

	return NULL;
}

static inline void set_bytes_used(struct net_buf *buf, uint32_t used)
{
	struct mpipe_buffer_meta *bm = mpipe_buffer_get_meta(buf);

	bm->bytes_used = used;
	buf->len = used;

	/* Keep Zephyr video_buffer (if any) in sync for vid_transform/video_enqueue */
	/* TODO: Ensure driver_buf is a video_buffer */
	if (bm->driver_buf != NULL) {
		((struct video_buffer *)bm->driver_buf)->bytesused = used;
	}
}

static int copy_jpeg_frame(struct net_buf *dst, const uint8_t *src, size_t len)
{
	struct mpipe_buffer_meta *bm;
	uint32_t cap;

	__ASSERT_NO_MSG(dst != NULL);
	__ASSERT_NO_MSG(src != NULL);

	bm = mpipe_buffer_get_meta(dst);
	cap = bm && bm->pool ? bm->pool->config.size : 0;
	if (cap < len) {
		return -ENOBUFS;
	}

	memcpy(dst->data, src, len);

	set_bytes_used(dst, (uint32_t)len);

	return 0;
}

static int append_to_partial(struct net_buf *partial, const uint8_t *src, size_t len)
{
	struct mpipe_buffer_meta *m;
	uint32_t used;
	uint32_t cap;

	__ASSERT_NO_MSG(partial != NULL);
	__ASSERT_NO_MSG(src != NULL);

	m = mpipe_buffer_get_meta(partial);
	used = m->bytes_used;
	cap = m->pool ? m->pool->config.size : 0;

	if (cap < used || (cap - used) < len) {
		return -ENOBUFS;
	}

	memcpy(partial->data + used, src, len);

	set_bytes_used(partial, used + (uint32_t)len);

	return 0;
}

/* Present a freshly allocated buffer as an empty buffer of @p pool */
static void mpipe_img_jpeg_parser_init_meta(struct net_buf *buf, struct mpipe_buffer_pool *pool)
{
	struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(buf);

	m->pool = pool;
	m->bytes_used = 0;
	m->timestamp = 0;
	m->driver_buf = NULL;
	m->priv = NULL;
	buf->len = 0;
}

static int mpipe_img_jpeg_parser_release_buffer(struct mpipe_buffer_pool *pool, struct net_buf *buf)
{
	ARG_UNUSED(pool);

	if (buf != NULL) {
		struct mpipe_buffer_meta *m = mpipe_buffer_get_meta(buf);

		if (m != NULL) {
			m->bytes_used = 0;
			m->timestamp = 0;
			m->driver_buf = NULL;
			m->priv = NULL;
		}
		buf->len = 0;
	}

	return 0;
}

static int mpipe_img_jpeg_parser_acquire_buffer(struct mpipe_buffer_pool *pool,
						struct net_buf **buf)
{
	struct mpipe_img_jpeg_parser *jpeg_parser;
	struct net_buf *out;

	if (pool == NULL || buf == NULL || pool->nb_pool == NULL) {
		return -EINVAL;
	}

	jpeg_parser = CONTAINER_OF(pool, struct mpipe_img_jpeg_parser, out_pool);

	/* Accumulating, the buffer being filled is the one to append to */
	if (jpeg_parser->accumulate_upstream && jpeg_parser->accum_buf != NULL) {
		*buf = net_buf_ref(jpeg_parser->accum_buf);
		return 0;
	}

	out = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_FOREVER);
	if (out == NULL) {
		return -ENOBUFS;
	}

	mpipe_img_jpeg_parser_init_meta(out, pool);

	if (jpeg_parser->accumulate_upstream) {
		/*
		 * This buffer becomes the one the stream accumulates into, so the
		 * parser takes a reference of its own and hands out a second one.
		 * The producer receiving into it therefore cannot recycle it
		 * underneath the parser: it is recycled once the parser has let
		 * go of it and it has travelled downstream.
		 */
		jpeg_parser->accum_buf = net_buf_ref(out);
		jpeg_parser->scan_offset = 0;
	}

	*buf = out;

	return 0;
}

/*
 * Offer the accumulation pool to the element upstream. The producer then
 * receives straight into the buffer the parser is assembling, which is what
 * removes the copy of every byte of every frame from the path.
 */
static int mpipe_img_jpeg_parser_propose_buffer_pool(struct mpipe_parser *parser,
						     struct mpipe_dispatch *query)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)parser;

	query->pool = &jpeg_parser->out_pool;
	query->pool_cfg = jpeg_parser->out_pool.config;

	return 0;
}

/*
 * The stream is a sequence of concatenated JPEG frames, so the terminator alone
 * delimits them: the buffer starts on a frame start, and each frame that follows
 * starts on the byte after the previous EOI. Matching the start marker as well
 * would buy nothing and cost a second scan of every byte.
 *
 * The buffer holds everything received so far, so scanning resumes where it
 * stopped instead of walking the bytes seen on the previous rounds. The head of
 * an unterminated frame is moved to the front of the buffer for the producer to
 * append after, so the buffer stays the one being filled.
 */
static int mpipe_img_jpeg_parser_parse_accum_buf(struct mpipe_img_jpeg_parser *jpeg_parser,
						 struct net_buf *in_buf, struct net_buf **out_buf)
{
	struct mpipe_buffer_pool *pool = &jpeg_parser->out_pool;
	const uint8_t *data = in_buf->data;
	uint32_t in_used = mpipe_buffer_get_meta(in_buf)->bytes_used;
	uint32_t cap = pool->config.size;
	uint32_t scan = jpeg_parser->scan_offset;
	uint32_t frame_start = 0;
	bool any_frame = false;
	int ret;

	*out_buf = NULL;

	if (scan > in_used) {
		scan = in_used;
	}

	while (true) {
		const uint8_t *marker;
		struct net_buf *out;
		uint32_t len;

		marker = find_jpeg_eoi(data + scan, in_used - scan);
		if (marker == NULL) {
			break;
		}

		len = (uint32_t)(marker - data) + 2U - frame_start;

		/*
		 * Not through the pool's acquire hook: that one hands back the
		 * buffer being assembled, which is the buffer being cut from here.
		 */
		out = net_buf_alloc_len(pool->nb_pool, pool->config.size, K_FOREVER);
		if (out == NULL) {
			LOG_ERR("Failed to acquire an output buffer");
			ret = -ENOBUFS;
			goto err;
		}

		mpipe_img_jpeg_parser_init_meta(out, pool);

		if (copy_jpeg_frame(out, data + frame_start, len) < 0) {
			LOG_ERR("Failed to copy a JPEG frame");
			net_buf_unref(out);
			ret = -ENOBUFS;
			goto err;
		}

		if (*out_buf == NULL) {
			*out_buf = out;
		} else {
			net_buf_frag_add(*out_buf, out);
		}
		any_frame = true;

		scan = (uint32_t)(marker - data) + 2U;
		frame_start = scan;
	}

	if (!any_frame) {
		/*
		 * Nothing to decode yet. The parser keeps its own reference on the
		 * buffer, so dropping the travelling one leaves it as the buffer the
		 * producer resumes receiving into. No byte is moved.
		 */
		jpeg_parser->scan_offset = (in_used > 0U) ? in_used - 1U : 0U;

		if (in_used >= cap) {
			/*
			 * A frame bigger than a whole buffer can never be completed.
			 * Drop the whole buffer and resume on the bytes that follow,
			 * otherwise the producer would be called with no room left and
			 * report the stream as closed.
			 */
			LOG_WRN("Dropping a JPEG frame larger than %u bytes", cap);
			set_bytes_used(in_buf, 0);
			jpeg_parser->scan_offset = 0;
		}

		net_buf_unref(in_buf);

		return 0;
	}

	/*
	 * The frames are out, so only the head of the unterminated one that
	 * follows the last EOI is worth keeping. Move it to the front and let the
	 * producer append after it.
	 */
	if (in_used > frame_start) {
		memmove(in_buf->data, data + frame_start, in_used - frame_start);
		set_bytes_used(in_buf, in_used - frame_start);
	} else {
		set_bytes_used(in_buf, 0);
	}

	in_used = mpipe_buffer_get_meta(in_buf)->bytes_used;
	jpeg_parser->scan_offset = (in_used > 0U) ? in_used - 1U : 0U;

	net_buf_unref(in_buf);

	return 0;

err:
	if (*out_buf != NULL) {
		net_buf_unref(*out_buf);
		*out_buf = NULL;
	}
	net_buf_unref(in_buf);

	return ret;
}

static int mpipe_img_jpeg_parser_chain_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
					  struct net_buf **out_buf)
{
	struct mpipe_parser *parser = (struct mpipe_parser *)pad->object.container;
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)parser;
	struct mpipe_buffer_pool *out_pool = parser->out_pool;
	const uint8_t *data = in_buf->data;
	uint32_t in_used = mpipe_buffer_get_meta(in_buf)->bytes_used;
	size_t parse_offset = 0;
	const uint8_t *eoi_ptr;

	*out_buf = NULL;

	/*
	 * The buffer the producer filled is the one the parser is assembling into,
	 * so the frames are cut out of it where they already sit.
	 */
	if (jpeg_parser->accumulate_upstream && in_buf == jpeg_parser->accum_buf) {
		return mpipe_img_jpeg_parser_parse_accum_buf(jpeg_parser, in_buf, out_buf);
	}

	/* If we have a partial buffer, try to complete it with this input buffer */
	if (jpeg_parser->partial_frame != NULL) {
		struct net_buf *partial = jpeg_parser->partial_frame;
		uint32_t partial_used = mpipe_buffer_get_meta(partial)->bytes_used;
		uint8_t *partial_eoi;
		bool boundary_eoi = false;

		/* Search EOI across [partial_frame + in_buf] by concatenating temporarily */
		/* First, look for EOI within partial itself */
		partial_eoi = find_jpeg_eoi(partial->data, partial_used);
		if (partial_eoi == NULL) {
			/*
			 * Look for EOI in the in_buf, but account for possible 0xFF at the end
			 * of partial
			 */
			bool boundary_ff = (partial_used > 0 &&
					    partial->data[partial_used - 1] == JPEG_EOI_BYTE0);
			boundary_eoi = (boundary_ff && in_used > 0 && data[0] == JPEG_EOI_BYTE1);

			eoi_ptr = find_jpeg_eoi(data, in_used);
			if (boundary_eoi) {
				/*
				 * EOI spans boundary: partial ends with 0xFF, in_buf starts with
				 * 0xD9
				 */
				eoi_ptr = data;
			}
		} else {
			eoi_ptr = partial_eoi;
		}

		if (eoi_ptr == NULL) {
			/* Still partial: append everything */
			if (append_to_partial(partial, data, in_used) < 0) {
				LOG_ERR("Partial buffer overflow");
				net_buf_unref(in_buf);
				return -ENOBUFS;
			}
			net_buf_unref(in_buf);
			return 0;
		}

		/* We have EOI; copy bytes up to and including EOI into partial */
		size_t to_copy;

		if (eoi_ptr == partial_eoi) {
			to_copy = 0;
		} else {
			/* eoi_ptr points into input buffer */
			to_copy = (size_t)(eoi_ptr - data) + 2U;
		}

		if (to_copy > 0) {
			if (append_to_partial(partial, data, to_copy) < 0) {
				LOG_ERR("Partial buffer overflow");
				net_buf_unref(in_buf);
				return -ENOBUFS;
			}
		}

		/* The partial buffer now holds a complete JPEG frame; output it */
		*out_buf = partial;
		jpeg_parser->partial_frame = NULL;
		parse_offset = boundary_eoi ? 1U : to_copy;
	}

	/* Parse remaining data in input buffer for complete frames */
	while (parse_offset < in_used &&
	       (eoi_ptr = find_jpeg_eoi(data + parse_offset, in_used - parse_offset)) != NULL) {
		struct net_buf *out = NULL;
		size_t up_to_eoi = (size_t)(eoi_ptr - data) + 2U;
		size_t len = up_to_eoi - parse_offset;

		if (out_pool->acquire_buffer(out_pool, &out) != 0 || out == NULL) {
			LOG_ERR("Failed to acquire output buffer");
			net_buf_unref(in_buf);
			if (*out_buf != NULL) {
				net_buf_unref(*out_buf);
				*out_buf = NULL;
			}
			return -ENOMEM;
		}

		if (copy_jpeg_frame(out, data + parse_offset, len) < 0) {
			LOG_ERR("Failed to copy JPEG frame");
			net_buf_unref(out);
			net_buf_unref(in_buf);
			if (*out_buf != NULL) {
				net_buf_unref(*out_buf);
				*out_buf = NULL;
			}
			return -ENOBUFS;
		}

		if (*out_buf == NULL) {
			*out_buf = out;
		} else {
			net_buf_frag_add(*out_buf, out);
		}

		parse_offset = up_to_eoi;
	}

	/* Any remaining bytes are a partial frame start: accumulate into partial_frame */
	if (parse_offset < in_used) {
		struct net_buf *partial = NULL;
		size_t remain = in_used - parse_offset;

		if (out_pool->acquire_buffer(out_pool, &partial) != 0 || partial == NULL) {
			LOG_ERR("Failed to acquire partial buffer");
			net_buf_unref(in_buf);
			if (*out_buf != NULL) {
				net_buf_unref(*out_buf);
				*out_buf = NULL;
			}
			return -ENOMEM;
		}

		if (copy_jpeg_frame(partial, data + parse_offset, remain) < 0) {
			LOG_ERR("Failed to init partial buffer");
			net_buf_unref(partial);
			net_buf_unref(in_buf);
			if (*out_buf != NULL) {
				net_buf_unref(*out_buf);
				*out_buf = NULL;
			}
			return -ENOBUFS;
		}

		jpeg_parser->partial_frame = partial;
	}

	net_buf_unref(in_buf);

	return 0;
}

static int mpipe_img_jpeg_parser_set_property(struct mpipe_object *obj, uint32_t key,
					      const void *val)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)obj;

	switch (key) {
	case MPIPE_PROP_IMG_JPEG_PARSER_ACCUMULATE_UPSTREAM:
		if (val == NULL) {
			return -EINVAL;
		}

		if (jpeg_parser->base.element.current_state != MPIPE_STATE_READY) {
			LOG_ERR("Accumulate upstream only changes in the READY state");
			return -EBUSY;
		}

		jpeg_parser->accumulate_upstream = *(const bool *)val;
		if (jpeg_parser->accumulate_upstream &&
		    (CONFIG_MPIPE_IMG_JPEG_PARSER_MAX_FRAME_SIZE == 0 ||
		     CONFIG_MPIPE_IMG_JPEG_PARSER_POOL_NUM <= 1)) {
			jpeg_parser->accumulate_upstream = false;
			LOG_ERR("Accumulating upstream needs a parser pool of at least "
				"two buffers with a non-zero size");
			return -ENOTSUP;
		}

		jpeg_parser->base.propose_buffer_pool =
			jpeg_parser->accumulate_upstream ? mpipe_img_jpeg_parser_propose_buffer_pool
							 : NULL;

		return 0;
	default:
		LOG_ERR("Property %u is unknown", key);
		return -ENOTSUP;
	}
}

static int mpipe_img_jpeg_parser_get_property(struct mpipe_object *obj, uint32_t key, void *val)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)obj;

	switch (key) {
	case MPIPE_PROP_IMG_JPEG_PARSER_ACCUMULATE_UPSTREAM:
		if (val == NULL) {
			return -EINVAL;
		}
		*(bool *)val = jpeg_parser->accumulate_upstream;
		return 0;
	default:
		LOG_ERR("Property %u is unknown", key);
		return -ENOTSUP;
	}
}

static int mpipe_img_jpeg_parser_change_state(struct mpipe_element *self,
					      enum mpipe_state_change transition)
{
	struct mpipe_img_jpeg_parser *jpeg_parser = (struct mpipe_img_jpeg_parser *)self;

	/*
	 * On teardown (PAUSED -> READY), drop any leftover bytes in partial frame.
	 * Otherwise, next stream's opening bytes get spliced onto these stale bytes,
	 * producing a corrupt JPEG between replays.
	 */
	if (transition == MPIPE_STATE_CHANGE_PAUSED_TO_READY) {
		if (jpeg_parser->partial_frame != NULL) {
			net_buf_unref(jpeg_parser->partial_frame);
			jpeg_parser->partial_frame = NULL;
		}

		if (jpeg_parser->accum_buf != NULL) {
			net_buf_unref(jpeg_parser->accum_buf);
			jpeg_parser->accum_buf = NULL;
		}

		jpeg_parser->scan_offset = 0;
	}

	/*
	 * Chain to the base parser change_state, which resets the negotiated pad
	 * caps back to the template caps on PAUSED_TO_READY so a subsequent
	 * re-negotiation starts fresh.
	 */
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
	jpeg_parser->partial_frame = NULL;
	jpeg_parser->accum_buf = NULL;
	jpeg_parser->scan_offset = 0;

	jpeg_parser->accumulate_upstream = false;

	self->object.set_property = mpipe_img_jpeg_parser_set_property;
	self->object.get_property = mpipe_img_jpeg_parser_get_property;

	/*
	 * The capabilities are enumerated from .rodata, so the ANY caps that
	 * mpipe_parser_init() leaves on both sides are never consulted.
	 */
	parser->sink_pad.enum_caps_fn = mpipe_img_jpeg_parser_enum_caps;
	parser->src_pad.enum_caps_fn = mpipe_img_jpeg_parser_enum_caps;

	parser->sink_pad.chain_fn = mpipe_img_jpeg_parser_chain_fn;
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
		/* net_buf pool is static; no explicit start */
		jpeg_parser->out_pool.started = true;
		parser->out_pool = &jpeg_parser->out_pool;
	}

	return 0;
}
