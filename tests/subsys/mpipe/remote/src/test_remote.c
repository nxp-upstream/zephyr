/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The client elements against the pipeline server, both in this image: the
 * channel of a client calls the server's functions directly, so everything
 * but a transport is exercised.
 *
 *   client side:  app_src -> vid_transform_client -> app_sink
 *   served:       app_src -> test_xform -> vid_overlay -> app_sink
 *
 *   client side:  app_src -> vid_sink_client
 *   served:       app_src -> caps_filter -> app_sink (standing for a real sink)
 *
 *   client side:  app_src -> vid_transform_client -> vid_transform_client -> app_sink
 *   served:       app_src -> test_xform -> app_sink, one pipeline for each client
 */

#include <zephyr/drivers/video.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#include <zephyr/mpipe/vid/mpipe_vid_pipeline_server.h>
#include <zephyr/mpipe/vid/mpipe_vid_sink_client.h>
#include <zephyr/mpipe/vid/mpipe_vid_transform_client.h>

ZBUS_MSG_SUBSCRIBER_DEFINE(test_remote_sub);

#define TEST_WIDTH   16U
#define TEST_HEIGHT  8U
#define TEST_FRAME   (TEST_WIDTH * TEST_HEIGHT * 2U)
#define TEST_BUFS    5U
#define TEST_MARK    0xA5U
#define TEST_TIMEOUT K_SECONDS(1)

BUILD_ASSERT(TEST_FRAME <= CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	SRC_ID,
	CLIENT_ID,
	SECOND_CLIENT_ID,
	SINK_ID,
	XFORM_ID,
	OVERLAY_ID,
	FILTER_ID,
};

/*
 * A transform allocating from the pool downstream proposes, as a decoder
 * does: each output byte is the input byte plus one.
 */
struct test_xform {
	struct mpipe_transform transform;
	/* Where the last output was written */
	const uint8_t *last_out;
	/* Produce nothing for the next input */
	bool drop;
};

#define TEST_XFORM_FIELDS(X) X(MPIPE_CAPS_PIXEL_FORMAT, MPIPE_VALUE_UINT(VIDEO_PIX_FMT_RGB565))

MPIPE_STRUCTURE_DEFINE(test_xform_caps, MPIPE_MEDIA_VIDEO, TEST_XFORM_FIELDS);

static int test_xform_enum_caps(struct mpipe_pad *pad, uint32_t index,
				const struct mpipe_structure *filter, struct mpipe_structure *out)
{
	ARG_UNUSED(pad);

	if (index > 0U) {
		return -ENOENT;
	}

	return mpipe_pad_enum_filter(&test_xform_caps, filter, out);
}

static int test_xform_decide_buffer_pool(struct mpipe_transform *self, struct mpipe_dispatch *query)
{
	self->out_pool = query->pool;

	return (query->pool != NULL) ? 0 : -ENOMEM;
}

static int test_xform_process_fn(struct mpipe_pad *pad, struct net_buf *in_buf,
				 struct net_buf **out_buf)
{
	struct test_xform *xform = (struct test_xform *)pad->object.container;
	struct mpipe_buffer_pool *pool = xform->transform.out_pool;
	uint32_t used = mpipe_buffer_get_meta(in_buf)->bytes_used;
	struct net_buf *out = NULL;

	*out_buf = NULL;

	if (xform->drop) {
		xform->drop = false;
		net_buf_unref(in_buf);
		return 0;
	}

	if (pool->acquire_buffer(pool, &out) != 0 || out == NULL || used > out->size) {
		net_buf_unref(in_buf);
		return -ENOBUFS;
	}

	for (uint32_t i = 0; i < used; i++) {
		out->data[i] = in_buf->data[i] + 1U;
	}

	mpipe_buffer_get_meta(out)->bytes_used = used;
	out->len = (uint16_t)used;
	xform->last_out = out->data;

	net_buf_unref(in_buf);
	*out_buf = out;

	return 0;
}

static int test_xform_init(struct test_xform *xform, uint8_t id)
{
	struct mpipe_transform *transform = &xform->transform;
	int ret = mpipe_transform_init(transform, id);

	if (ret != 0) {
		return ret;
	}

	transform->mode = MPIPE_TRANSFORM_MODE_NORMAL;
	transform->out_pool = NULL;
	transform->sink_pad.enum_caps_fn = test_xform_enum_caps;
	transform->src_pad.enum_caps_fn = test_xform_enum_caps;
	transform->sink_pad.process_fn = test_xform_process_fn;
	transform->decide_buffer_pool = test_xform_decide_buffer_pool;

	return 0;
}

/* The second served element marks the last byte of every frame */
static void overlay_draw(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
			 const struct net_buf *in, void *user_data)
{
	ARG_UNUSED(pixfmt);
	ARG_UNUSED(in);
	ARG_UNUSED(user_data);

	frame[(uint32_t)width * height * 2U - 1U] = TEST_MARK;
}

/* What the pipeline serving the sink client consumed */
static struct {
	atomic_t count;
	uint8_t first_byte[TEST_BUFS];
	uint32_t bytes_used[TEST_BUFS];
} consumed;

static void served_sink_cb(const struct net_buf *buf, void *user_data)
{
	uint32_t i = (uint32_t)atomic_inc(&consumed.count);

	ARG_UNUSED(user_data);

	if (i < TEST_BUFS) {
		consumed.first_byte[i] = buf->data[0];
		consumed.bytes_used[i] = mpipe_buffer_get_meta(buf)->bytes_used;
	}
}

/*
 * What an RPC backend does, without the transport: the channel of a client
 * calls the functions of the server it is bound to.
 */
struct test_channel {
	struct mpipe_rpc_channel channel;
	struct mpipe_pipeline_server *server;
};

static struct {
	/* Transform client and the pipeline it sits in */
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct mpipe_vid_transform_client client;
	struct test_channel channel;
	struct mpipe_app_sink app_sink;
	/* The pipeline served to it */
	struct mpipe served;
	struct mpipe_app_src served_src;
	struct test_xform xform;
	struct mpipe_vid_overlay overlay;
	struct mpipe_app_sink served_sink;
	struct mpipe_pipeline_server server;
} xf;

static struct {
	/* Sink client and the pipeline it sits in */
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct mpipe_vid_sink_client client;
	struct test_channel channel;
	/* The pipeline served to it */
	struct mpipe served;
	struct mpipe_app_src served_src;
	struct mpipe_caps_filter filter;
	struct mpipe_app_sink served_sink;
	struct mpipe_pipeline_server server;
} sk;

/* A pipeline served to one of two transform clients of the same kind */
struct test_served {
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct test_xform xform;
	struct mpipe_app_sink app_sink;
	struct mpipe_pipeline_server server;
	struct test_channel channel;
};

static struct {
	struct mpipe pipeline;
	struct mpipe_app_src app_src;
	struct mpipe_vid_transform_client client[2];
	struct mpipe_app_sink app_sink;
	struct test_served served[2];
} two;

static struct mpipe_pipeline_server *channel_server(struct mpipe_rpc_channel *channel)
{
	return CONTAINER_OF(channel, struct test_channel, channel)->server;
}

static int test_channel_open(struct mpipe_rpc_channel *channel)
{
	ARG_UNUSED(channel);

	return 0;
}

static int test_channel_process(struct mpipe_rpc_channel *channel, uintptr_t in_buf, uint32_t in_sz,
				uintptr_t out_buf, uint32_t out_cap, uint32_t *out_sz)
{
	return mpipe_pipeline_server_process(channel_server(channel), (void *)in_buf, in_sz,
					     (void *)out_buf, out_cap, out_sz);
}

static int test_channel_set_state(struct mpipe_rpc_channel *channel,
				  enum mpipe_state_change transition)
{
	return mpipe_pipeline_server_set_state(channel_server(channel),
					       MPIPE_STATE_TRANSITION_NEXT(transition));
}

static int test_channel_eos(struct mpipe_rpc_channel *channel)
{
	return mpipe_pipeline_server_eos(channel_server(channel));
}

static const struct mpipe_rpc_channel_api test_channel_api = {
	.open = test_channel_open,
	.process = test_channel_process,
	.set_state = test_channel_set_state,
	.eos = test_channel_eos,
};

static int test_get_buf_caps(struct mpipe_rpc_channel *channel, enum mpipe_pad_direction direction,
			     uint8_t *min_buffers, uint16_t *buf_align)
{
	return mpipe_vid_pipeline_server_get_buf_caps(channel_server(channel), direction,
						      min_buffers, buf_align);
}

static int test_get_format_caps(struct mpipe_rpc_channel *channel,
				enum mpipe_pad_direction direction, uint8_t ind,
				struct video_format_cap *vfc)
{
	return mpipe_vid_pipeline_server_get_format_caps(channel_server(channel), direction, ind,
							 vfc);
}

static int test_set_format(struct mpipe_rpc_channel *channel, struct video_format *fmt)
{
	return mpipe_vid_pipeline_server_set_format(channel_server(channel), fmt);
}

static int test_transform_cap(struct mpipe_rpc_channel *channel, enum mpipe_pad_direction direction,
			      uint16_t ind, const struct video_format_cap *vfc,
			      struct video_format_cap *other_vfc)
{
	return mpipe_vid_pipeline_server_transform_cap(channel_server(channel), direction, ind, vfc,
						       other_vfc);
}

static const struct mpipe_vid_rpc_format_api test_format_api = {
	.get_buf_caps = test_get_buf_caps,
	.get_format_caps = test_get_format_caps,
	.set_format = test_set_format,
	.transform_cap = test_transform_cap,
};

static void frame_caps(struct mpipe_structure *caps, uint32_t pixfmt)
{
	zassert_ok(mpipe_structure_init_fields(caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					       MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_IMAGE_WIDTH,
					       MPIPE_TYPE_UINT, TEST_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT,
					       MPIPE_TYPE_UINT, TEST_HEIGHT, MPIPE_CAPS_END));
}

static void set_src_caps(struct mpipe_app_src *app_src, uint32_t pixfmt)
{
	struct mpipe_structure caps;

	frame_caps(&caps, pixfmt);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)app_src,
					       MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
}

static void build_transform_side(void)
{
	static const struct mpipe_vid_overlay_cb draw = {.fn = overlay_draw};

	memset(&xf, 0, sizeof(xf));

	/* The served pipeline and its server come first: the client talks to them at init */
	zassert_ok(mpipe_pipeline_init(&xf.served, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&xf.served_src, SRC_ID));
	zassert_ok(test_xform_init(&xf.xform, XFORM_ID));
	zassert_ok(mpipe_vid_overlay_init(&xf.overlay, OVERLAY_ID));
	zassert_ok(mpipe_app_sink_init(&xf.served_sink, SINK_ID));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&xf.overlay,
					       MPIPE_PROP_VID_OVERLAY_DRAW_CB, &draw,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&xf.served, (struct mpipe_element *)&xf.served_src,
		(struct mpipe_element *)&xf.xform, (struct mpipe_element *)&xf.overlay,
		(struct mpipe_element *)&xf.served_sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&xf.served_src,
				      (struct mpipe_element *)&xf.xform,
				      (struct mpipe_element *)&xf.overlay,
				      (struct mpipe_element *)&xf.served_sink, NULL));
	zassert_ok(mpipe_pipeline_server_init(&xf.server, &xf.served, &xf.served_src,
					      &xf.served_sink));

	xf.channel.server = &xf.server;
	mpipe_rpc_channel_init(&xf.channel.channel, &test_channel_api);
	xf.client.transform.channel = &xf.channel.channel;
	xf.client.format = &test_format_api;

	zassert_ok(mpipe_pipeline_init(&xf.pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&xf.app_src, SRC_ID));
	zassert_ok(mpipe_vid_transform_client_init(&xf.client, CLIENT_ID));
	zassert_ok(mpipe_app_sink_init(&xf.app_sink, SINK_ID));
	set_src_caps(&xf.app_src, VIDEO_PIX_FMT_RGB565);
	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&xf.pipeline, (struct mpipe_element *)&xf.app_src,
		(struct mpipe_element *)&xf.client, (struct mpipe_element *)&xf.app_sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&xf.app_src,
				      (struct mpipe_element *)&xf.client,
				      (struct mpipe_element *)&xf.app_sink, NULL));
}

static void build_sink_side(void)
{
	static const struct mpipe_app_sink_cb cb = {.fn = served_sink_cb};
	struct mpipe_structure caps;

	memset(&sk, 0, sizeof(sk));
	memset(&consumed, 0, sizeof(consumed));

	zassert_ok(mpipe_pipeline_init(&sk.served, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&sk.served_src, SRC_ID));
	zassert_ok(mpipe_caps_filter_init(&sk.filter, FILTER_ID));
	zassert_ok(mpipe_app_sink_init(&sk.served_sink, SINK_ID));
	zassert_ok(mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					       MPIPE_TYPE_UINT, VIDEO_PIX_FMT_RGB565,
					       MPIPE_CAPS_END));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&sk.filter,
					       MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&sk.served_sink,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));
	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&sk.served, (struct mpipe_element *)&sk.served_src,
		(struct mpipe_element *)&sk.filter, (struct mpipe_element *)&sk.served_sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&sk.served_src,
				      (struct mpipe_element *)&sk.filter,
				      (struct mpipe_element *)&sk.served_sink, NULL));
	/* The pipeline ends in a sink of its own: the server is given no application sink */
	zassert_ok(mpipe_pipeline_server_init(&sk.server, &sk.served, &sk.served_src, NULL));

	sk.channel.server = &sk.server;
	mpipe_rpc_channel_init(&sk.channel.channel, &test_channel_api);
	sk.client.sink.channel = &sk.channel.channel;
	sk.client.format = &test_format_api;

	zassert_ok(mpipe_pipeline_init(&sk.pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&sk.app_src, SRC_ID));
	zassert_ok(mpipe_vid_sink_client_init(&sk.client, CLIENT_ID));
	set_src_caps(&sk.app_src, VIDEO_PIX_FMT_RGB565);
	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&sk.pipeline,
				 (struct mpipe_element *)&sk.app_src,
				 (struct mpipe_element *)&sk.client, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&sk.app_src,
				      (struct mpipe_element *)&sk.client, NULL));
}

/* app_src -> vid_transform_client -> vid_transform_client -> app_sink, a server each */
static void build_two_clients(void)
{
	memset(&two, 0, sizeof(two));

	for (uint8_t i = 0; i < ARRAY_SIZE(two.served); i++) {
		struct test_served *served = &two.served[i];

		zassert_ok(mpipe_pipeline_init(&served->pipeline, PIPE_ID));
		zassert_ok(mpipe_app_src_init(&served->app_src, SRC_ID));
		zassert_ok(test_xform_init(&served->xform, XFORM_ID));
		zassert_ok(mpipe_app_sink_init(&served->app_sink, SINK_ID));
		zassert_ok(mpipe_bin_add((struct mpipe_bin *)&served->pipeline,
					 (struct mpipe_element *)&served->app_src,
					 (struct mpipe_element *)&served->xform,
					 (struct mpipe_element *)&served->app_sink, NULL));
		zassert_ok(mpipe_element_link((struct mpipe_element *)&served->app_src,
					      (struct mpipe_element *)&served->xform,
					      (struct mpipe_element *)&served->app_sink, NULL));
		zassert_ok(mpipe_pipeline_server_init(&served->server, &served->pipeline,
						      &served->app_src, &served->app_sink));

		served->channel.server = &served->server;
		mpipe_rpc_channel_init(&served->channel.channel, &test_channel_api);
		two.client[i].transform.channel = &served->channel.channel;
		two.client[i].format = &test_format_api;
	}

	zassert_ok(mpipe_pipeline_init(&two.pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&two.app_src, SRC_ID));
	zassert_ok(mpipe_vid_transform_client_init(&two.client[0], CLIENT_ID));
	zassert_ok(mpipe_vid_transform_client_init(&two.client[1], SECOND_CLIENT_ID));
	zassert_ok(mpipe_app_sink_init(&two.app_sink, SINK_ID));
	set_src_caps(&two.app_src, VIDEO_PIX_FMT_RGB565);
	zassert_ok(mpipe_bin_add(
		(struct mpipe_bin *)&two.pipeline, (struct mpipe_element *)&two.app_src,
		(struct mpipe_element *)&two.client[0], (struct mpipe_element *)&two.client[1],
		(struct mpipe_element *)&two.app_sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&two.app_src,
				      (struct mpipe_element *)&two.client[0],
				      (struct mpipe_element *)&two.client[1],
				      (struct mpipe_element *)&two.app_sink, NULL));
}

static void remote_before(void *f)
{
	ARG_UNUSED(f);

	build_transform_side();
	build_sink_side();
	build_two_clients();
}

static void remote_after(void *f)
{
	ARG_UNUSED(f);

	/* Also runs when the test failed before tearing its pipelines down */
	(void)mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY);
	(void)mpipe_element_set_state((struct mpipe_element *)&sk.pipeline, MPIPE_STATE_READY);
	(void)mpipe_element_set_state((struct mpipe_element *)&xf.served, MPIPE_STATE_READY);
	(void)mpipe_element_set_state((struct mpipe_element *)&sk.served, MPIPE_STATE_READY);
	(void)mpipe_element_set_state((struct mpipe_element *)&two.pipeline, MPIPE_STATE_READY);
	for (uint8_t i = 0; i < ARRAY_SIZE(two.served); i++) {
		(void)mpipe_element_set_state((struct mpipe_element *)&two.served[i].pipeline,
					      MPIPE_STATE_READY);
	}
}

ZTEST_SUITE(test_remote, NULL, NULL, remote_before, remote_after, NULL);

static enum mpipe_state state_of(struct mpipe *pipeline)
{
	return ((struct mpipe_element *)pipeline)->current_state;
}

static void push_frame(struct mpipe_app_src *app_src, uint8_t seed)
{
	uint8_t frame[TEST_FRAME];

	for (uint32_t i = 0; i < TEST_FRAME; i++) {
		frame[i] = (uint8_t)(seed + i);
	}

	zassert_ok(mpipe_app_src_push(app_src, frame, TEST_FRAME, TEST_TIMEOUT));
}

/* One frame through the transform client, checked byte by byte */
static void transform_one(uint8_t seed)
{
	struct net_buf *buf;

	push_frame(&xf.app_src, seed);
	zassert_ok(mpipe_app_sink_pull(&xf.app_sink, &buf, TEST_TIMEOUT), "no output");
	zassert_equal(mpipe_buffer_get_meta(buf)->bytes_used, TEST_FRAME);

	/* The first served element added one to every byte, the second marked the last */
	for (uint32_t i = 0; i < TEST_FRAME - 1U; i++) {
		zassert_equal(buf->data[i], (uint8_t)(seed + i + 1U), "byte %u", i);
	}
	zassert_equal(buf->data[TEST_FRAME - 1U], TEST_MARK, "the overlay did not draw");

	/* The served pipeline wrote into the client's buffer: nothing was copied back */
	zassert_equal_ptr(xf.xform.last_out, buf->data, "the output was copied");

	net_buf_unref(buf);
}

static void wait_eos(struct mpipe *pipeline)
{
	struct zbus_channel *bus = mpipe_element_get_bus_chan((struct mpipe_element *)pipeline);
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	zassert_ok(zbus_chan_add_obs(bus, &test_remote_sub, K_FOREVER));
	zassert_ok(zbus_sub_wait_msg(&test_remote_sub, &chan, &msg, TEST_TIMEOUT), "no EOS");
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS);
	zassert_ok(zbus_chan_rm_obs(bus, &test_remote_sub, K_FOREVER));
}

ZTEST(test_remote, test_transform)
{
	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_PLAYING));

	/* The served pipeline negotiated with the client's formats and followed its state */
	zassert_true(xf.server.negotiated);
	zassert_equal(state_of(&xf.served), MPIPE_STATE_PLAYING);

	for (uint8_t i = 0; i < TEST_BUFS; i++) {
		transform_one((uint8_t)(i * 10U));
	}

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY));
	zassert_equal(state_of(&xf.served), MPIPE_STATE_READY);
	zassert_false(xf.server.negotiated);
}

ZTEST(test_remote, test_transform_replay)
{
	for (uint8_t run = 0; run < 3U; run++) {
		zassert_ok(mpipe_element_set_state((struct mpipe_element *)&xf.pipeline,
						   MPIPE_STATE_PLAYING),
			   "run %u", run);
		transform_one(run);
		zassert_ok(mpipe_element_set_state((struct mpipe_element *)&xf.pipeline,
						   MPIPE_STATE_READY),
			   "run %u", run);
	}
}

ZTEST(test_remote, test_transform_drop)
{
	struct net_buf *buf;

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_PLAYING));

	/* A served element that produces nothing ends the push on both sides, with no error */
	xf.xform.drop = true;
	push_frame(&xf.app_src, 0);
	zassert_equal(mpipe_app_sink_pull(&xf.app_sink, &buf, K_MSEC(50)), -EAGAIN);

	transform_one(7);

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY));
}

ZTEST(test_remote, test_transform_eos)
{
	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_PLAYING));
	transform_one(1);

	zassert_ok(mpipe_app_src_eos(&xf.app_src, TEST_TIMEOUT));
	wait_eos(&xf.pipeline);

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY));
}

ZTEST(test_remote, test_transform_refused_format)
{
	/* A format no served element takes: the client's pipeline cannot negotiate */
	set_src_caps(&xf.app_src, VIDEO_PIX_FMT_GREY);

	zassert_not_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_PAUSED));
	zassert_false(xf.server.negotiated);

	(void)mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY);

	/* And it recovers with one they take */
	set_src_caps(&xf.app_src, VIDEO_PIX_FMT_RGB565);
	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_PLAYING));
	transform_one(3);
	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&xf.pipeline, MPIPE_STATE_READY));
}

ZTEST(test_remote, test_two_transform_clients)
{
	struct net_buf *buf;

	zassert_ok(mpipe_element_set_state((struct mpipe_element *)&two.pipeline,
					   MPIPE_STATE_PLAYING));

	/* Each client took its own server through the negotiation and the states */
	for (uint8_t i = 0; i < ARRAY_SIZE(two.served); i++) {
		zassert_true(two.served[i].server.negotiated, "server %u", i);
		zassert_equal(state_of(&two.served[i].pipeline), MPIPE_STATE_PLAYING, "server %u",
			      i);
	}

	for (uint8_t seed = 0; seed < TEST_BUFS; seed++) {
		push_frame(&two.app_src, seed);
		zassert_ok(mpipe_app_sink_pull(&two.app_sink, &buf, TEST_TIMEOUT), "no output");
		zassert_equal(mpipe_buffer_get_meta(buf)->bytes_used, TEST_FRAME);

		/* Each served pipeline added one to every byte */
		for (uint32_t i = 0; i < TEST_FRAME; i++) {
			zassert_equal(buf->data[i], (uint8_t)(seed + i + 2U), "byte %u", i);
		}

		/* The frame left the second server, whose input the first one wrote */
		zassert_equal_ptr(two.served[1].xform.last_out, buf->data);
		zassert_not_equal((uintptr_t)two.served[0].xform.last_out, (uintptr_t)buf->data);

		net_buf_unref(buf);
	}

	zassert_ok(mpipe_app_src_eos(&two.app_src, TEST_TIMEOUT));
	wait_eos(&two.pipeline);

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&two.pipeline, MPIPE_STATE_READY));
	for (uint8_t i = 0; i < ARRAY_SIZE(two.served); i++) {
		zassert_equal(state_of(&two.served[i].pipeline), MPIPE_STATE_READY, "server %u", i);
	}
}

ZTEST(test_remote, test_sink)
{
	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&sk.pipeline, MPIPE_STATE_PLAYING));
	zassert_true(sk.server.negotiated);
	zassert_equal(state_of(&sk.served), MPIPE_STATE_PLAYING);

	for (uint8_t i = 0; i < TEST_BUFS; i++) {
		push_frame(&sk.app_src, (uint8_t)(i + 1U));
	}

	/* The end of stream is posted once the served pipeline consumed everything */
	zassert_ok(mpipe_app_src_eos(&sk.app_src, TEST_TIMEOUT));
	wait_eos(&sk.pipeline);

	zassert_equal(atomic_get(&consumed.count), TEST_BUFS);
	for (uint8_t i = 0; i < TEST_BUFS; i++) {
		zassert_equal(consumed.first_byte[i], i + 1U, "buffer %u", i);
		zassert_equal(consumed.bytes_used[i], TEST_FRAME, "buffer %u", i);
	}

	zassert_ok(
		mpipe_element_set_state((struct mpipe_element *)&sk.pipeline, MPIPE_STATE_READY));
	zassert_equal(state_of(&sk.served), MPIPE_STATE_READY);
}

ZTEST(test_remote, test_server_calls)
{
	uint8_t in[TEST_FRAME] = {0};
	uint8_t out[TEST_FRAME];
	struct mpipe_structure caps;
	struct mpipe_structure other;
	uint32_t used = 1;

	/* Nothing flows through a pipeline that has not negotiated */
	zassert_equal(
		mpipe_pipeline_server_process(&xf.server, in, sizeof(in), out, sizeof(out), &used),
		-ENOTCONN);
	zassert_equal(mpipe_pipeline_server_set_state(&xf.server, MPIPE_STATE_PLAYING), -ENOTCONN);
	zassert_ok(mpipe_pipeline_server_eos(&xf.server));

	/* The entry and the exit are the served elements' outer pads */
	zassert_ok(mpipe_pipeline_server_enum_caps(&xf.server, MPIPE_PAD_SINK, 0, &caps));
	zassert_equal(mpipe_pipeline_server_enum_caps(&xf.server, MPIPE_PAD_SINK, 1, &caps),
		      -ENOENT);
	zassert_ok(mpipe_pipeline_server_enum_caps(&xf.server, MPIPE_PAD_SRC, 0, &caps));
	zassert_equal(mpipe_pipeline_server_enum_caps(&sk.server, MPIPE_PAD_SRC, 0, &caps),
		      -ENOTSUP);

	/* A capability crosses the whole pipeline, both ways, and only where it can */
	frame_caps(&caps, VIDEO_PIX_FMT_RGB565);
	zassert_ok(
		mpipe_pipeline_server_transform_caps(&xf.server, MPIPE_PAD_SRC, 0, &caps, &other));
	zassert_equal(
		mpipe_value_get_uint(mpipe_structure_get_value(&other, MPIPE_CAPS_IMAGE_WIDTH)),
		TEST_WIDTH);
	zassert_equal(
		mpipe_pipeline_server_transform_caps(&xf.server, MPIPE_PAD_SRC, 1, &caps, &other),
		-ENOENT);
	zassert_ok(
		mpipe_pipeline_server_transform_caps(&xf.server, MPIPE_PAD_SINK, 0, &caps, &other));
	frame_caps(&caps, VIDEO_PIX_FMT_GREY);
	zassert_equal(
		mpipe_pipeline_server_transform_caps(&xf.server, MPIPE_PAD_SRC, 0, &caps, &other),
		-ENOENT);

	/* The formats start the negotiation, the exit one last */
	frame_caps(&caps, VIDEO_PIX_FMT_RGB565);
	zassert_ok(mpipe_pipeline_server_set_caps(&xf.server, MPIPE_PAD_SINK, &caps));
	zassert_false(xf.server.negotiated);
	zassert_ok(mpipe_pipeline_server_set_caps(&xf.server, MPIPE_PAD_SRC, &caps));
	zassert_true(xf.server.negotiated);
	zassert_equal(state_of(&xf.served), MPIPE_STATE_PAUSED);

	in[0] = 41;
	zassert_ok(
		mpipe_pipeline_server_process(&xf.server, in, sizeof(in), out, sizeof(out), &used));
	zassert_equal(used, TEST_FRAME);
	zassert_equal(out[0], 42);

	/* An output that does not fit is refused, not truncated */
	zassert_not_ok(mpipe_pipeline_server_process(&xf.server, in, sizeof(in), out, 8, &used));

	zassert_ok(mpipe_pipeline_server_set_state(&xf.server, MPIPE_STATE_READY));
}
