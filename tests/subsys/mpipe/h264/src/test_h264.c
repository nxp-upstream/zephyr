/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_decoder.h>
#include <zephyr/mpipe/vid/mpipe_vid_h264_encoder.h>

ZBUS_MSG_SUBSCRIBER_DEFINE(test_h264_sub);

/* Element IDs (values are arbitrary; only uniqueness within a pipeline matters) */
enum {
	ENC_PIPE_ID,
	DEC_PIPE_ID,
	SRC_ID,
	CODEC_ID,
	SINK_ID,
};

#define TEST_FRAMES_NUM       8
#define TEST_MAX_WIDTH        96
#define TEST_MAX_HEIGHT       64
#define TEST_FRAME_SIZE(w, h) ((w) * (h) * 3 / 2)

/* Mean luma error a frame may show after encoding at TEST_QP and decoding */
#define TEST_MAX_LUMA_ERROR 6
#define TEST_QP             20

BUILD_ASSERT(TEST_FRAME_SIZE(TEST_MAX_WIDTH, TEST_MAX_HEIGHT) <= CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ,
	     "the application source must hold one frame");

/* The access units the encoder produced */
static struct {
	atomic_t count;
	uint32_t size[TEST_FRAMES_NUM];
	uint64_t pts[TEST_FRAMES_NUM];
	uint8_t data[TEST_FRAMES_NUM][CONFIG_MPIPE_VID_H264_ENCODER_MAX_OUT_FRAME_SIZE];
} encoded;

/* The frames the decoder produced */
static struct {
	atomic_t count;
	uint32_t size[TEST_FRAMES_NUM];
	uint64_t pts[TEST_FRAMES_NUM];
	uint32_t luma_error[TEST_FRAMES_NUM];
	/* Number of the source frame each decoded frame is compared with */
	uint8_t expected[TEST_FRAMES_NUM];
	uint16_t width;
	uint16_t height;
} decoded;

static atomic_t key_frame_requests;

/* A moving gradient: smooth enough to compress, different in every frame */
static void fill_frame(uint8_t *frame, uint16_t width, uint16_t height, uint8_t n)
{
	uint8_t *chroma = frame + (width * height);

	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			frame[(y * width) + x] = (uint8_t)(32 + x + (y / 2) + (4 * n));
		}
	}

	memset(chroma, 128, (width * height) / 2);
}

static void encoded_cb(const struct net_buf *buf, void *user_data)
{
	uint32_t i = (uint32_t)atomic_inc(&encoded.count);
	uint32_t size = mpipe_buffer_get_meta(buf)->bytes_used;

	ARG_UNUSED(user_data);

	if (i < TEST_FRAMES_NUM && size <= sizeof(encoded.data[i])) {
		memcpy(encoded.data[i], buf->data, size);
		encoded.size[i] = size;
		encoded.pts[i] = mpipe_buffer_get_meta(buf)->pts;
	}
}

static void decoded_cb(const struct net_buf *buf, void *user_data)
{
	static uint8_t reference[TEST_FRAME_SIZE(TEST_MAX_WIDTH, TEST_MAX_HEIGHT)];
	uint32_t i = (uint32_t)atomic_inc(&decoded.count);
	uint32_t luma = (uint32_t)decoded.width * decoded.height;
	uint32_t error = 0;

	ARG_UNUSED(user_data);

	if (i >= TEST_FRAMES_NUM) {
		return;
	}

	decoded.size[i] = mpipe_buffer_get_meta(buf)->bytes_used;
	decoded.pts[i] = mpipe_buffer_get_meta(buf)->pts;

	fill_frame(reference, decoded.width, decoded.height, decoded.expected[i]);
	for (uint32_t p = 0; p < luma; p++) {
		error += (uint32_t)abs((int)buf->data[p] - (int)reference[p]);
	}
	decoded.luma_error[i] = error / luma;
}

static void key_frame_cb(struct mpipe_vid_h264_decoder *dec, void *user_data)
{
	ARG_UNUSED(dec);
	ARG_UNUSED(user_data);

	atomic_inc(&key_frame_requests);
}

struct test_h264_fixture {
	struct mpipe enc_pipeline;
	struct mpipe_app_src enc_src;
	struct mpipe_vid_h264_encoder encoder;
	struct mpipe_app_sink enc_sink;
	struct mpipe dec_pipeline;
	struct mpipe_app_src dec_src;
	struct mpipe_vid_h264_decoder decoder;
	struct mpipe_app_sink dec_sink;
};

static void *h264_suite_setup(void)
{
	static struct test_h264_fixture fixture;

	return &fixture;
}

static void video_caps(struct mpipe_structure *caps, uint32_t pixfmt, uint16_t width,
		       uint16_t height)
{
	zassert_ok(mpipe_structure_init_fields(caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					       MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_IMAGE_WIDTH,
					       MPIPE_TYPE_UINT, width, MPIPE_CAPS_IMAGE_HEIGHT,
					       MPIPE_TYPE_UINT, height, MPIPE_CAPS_END),
		   "init caps failed");
}

/* Build src -> codec -> sink in a pipeline, with a sink callback */
static void build(struct mpipe *pipeline, uint8_t id, struct mpipe_app_src *src,
		  struct mpipe_element *codec, struct mpipe_app_sink *sink, mpipe_app_sink_cb_t fn)
{
	const struct mpipe_app_sink_cb cb = {.fn = fn};
	struct zbus_channel *bus;

	zassert_ok(mpipe_pipeline_init(pipeline, id));
	zassert_ok(mpipe_app_src_init(src, SRC_ID));
	zassert_ok(mpipe_app_sink_init(sink, SINK_ID));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)sink,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_bin_add((struct mpipe_bin *)pipeline, (struct mpipe_element *)src, codec,
				 (struct mpipe_element *)sink, NULL),
		   "Failed to add elements");
	zassert_ok(mpipe_element_link((struct mpipe_element *)src, codec,
				      (struct mpipe_element *)sink, NULL),
		   "Failed to link elements");

	bus = mpipe_element_get_bus_chan((struct mpipe_element *)pipeline);
	zassert_ok(zbus_chan_add_obs(bus, &test_h264_sub, K_FOREVER));
}

static void h264_before(void *f)
{
	static const struct mpipe_vid_h264_decoder_cb cb = {.fn = key_frame_cb};
	static const uint8_t qp = TEST_QP;
	/* Key frames at the start and on request only */
	static const uint16_t gop;
	struct test_h264_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));
	memset(&encoded, 0, sizeof(encoded));
	memset(&decoded, 0, sizeof(decoded));
	atomic_set(&key_frame_requests, 0);

	zassert_ok(mpipe_vid_h264_encoder_init(&fix->encoder, CODEC_ID));
	zassert_ok(mpipe_object_set_properties(
		(struct mpipe_object *)&fix->encoder, MPIPE_PROP_VID_H264_ENCODER_QP, &qp,
		MPIPE_PROP_VID_H264_ENCODER_GOP, &gop, MPIPE_PROP_LIST_END));
	build(&fix->enc_pipeline, ENC_PIPE_ID, &fix->enc_src, (struct mpipe_element *)&fix->encoder,
	      &fix->enc_sink, encoded_cb);

	zassert_ok(mpipe_vid_h264_decoder_init(&fix->decoder, CODEC_ID));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->decoder,
					       MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB, &cb,
					       MPIPE_PROP_LIST_END));
	build(&fix->dec_pipeline, DEC_PIPE_ID, &fix->dec_src, (struct mpipe_element *)&fix->decoder,
	      &fix->dec_sink, decoded_cb);
}

static void h264_after(void *f)
{
	struct test_h264_fixture *fix = f;
	struct mpipe *pipelines[] = {&fix->enc_pipeline, &fix->dec_pipeline};

	/* Also runs when the test failed before tearing its pipelines down */
	ARRAY_FOR_EACH(pipelines, i) {
		struct mpipe_element *pipe = (struct mpipe_element *)pipelines[i];

		(void)mpipe_element_set_state(pipe, MPIPE_STATE_READY);
		(void)zbus_chan_rm_obs(mpipe_element_get_bus_chan(pipe), &test_h264_sub, K_FOREVER);
	}
}

ZTEST_SUITE(test_h264, NULL, h264_suite_setup, h264_before, h264_after, NULL);

static void set_caps(struct mpipe_app_src *src, struct mpipe_app_sink *sink, uint32_t in_pixfmt,
		     uint32_t out_pixfmt, uint16_t width, uint16_t height)
{
	struct mpipe_structure caps;

	video_caps(&caps, in_pixfmt, width, height);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)src,
					       MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
	video_caps(&caps, out_pixfmt, width, height);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)sink,
					       MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
}

static void expect_eos(void)
{
	const struct zbus_channel *chan;
	struct mpipe_message msg;

	zassert_ok(zbus_sub_wait_msg(&test_h264_sub, &chan, &msg, K_SECONDS(5)),
		   "timed out waiting for a pipeline message");
	zassert_equal(msg.type, MPIPE_MESSAGE_EOS, "expected EOS, got %d", msg.type);
}

/*
 * Encode TEST_FRAMES_NUM frames of the moving gradient into the encoded
 * array, asking for a key frame before the frames set in @p key_frames.
 */
static void encode_all(struct test_h264_fixture *fix, uint16_t width, uint16_t height,
		       uint32_t key_frames)
{
	static uint8_t frame[TEST_FRAME_SIZE(TEST_MAX_WIDTH, TEST_MAX_HEIGHT)];
	static const bool request = true;
	struct mpipe_element *pipe = (struct mpipe_element *)&fix->enc_pipeline;

	set_caps(&fix->enc_src, &fix->enc_sink, VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_H264, width,
		 height);
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));

	for (uint8_t n = 0; n < TEST_FRAMES_NUM; n++) {
		if ((key_frames & BIT(n)) != 0U) {
			/* The frames pushed so far must be encoded before the request */
			while (atomic_get(&encoded.count) < n) {
				k_msleep(1);
			}
			zassert_ok(
				mpipe_object_set_properties((struct mpipe_object *)&fix->encoder,
							    MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME,
							    &request, MPIPE_PROP_LIST_END));
		}

		fill_frame(frame, width, height, n);
		zassert_ok(mpipe_app_src_push(&fix->enc_src, frame, TEST_FRAME_SIZE(width, height),
					      K_SECONDS(1)),
			   "Failed to push frame %u", n);
	}

	zassert_ok(mpipe_app_src_eos(&fix->enc_src, K_SECONDS(1)));
	expect_eos();
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY));

	zassert_equal(atomic_get(&encoded.count), TEST_FRAMES_NUM, "encoded %ld frames",
		      atomic_get(&encoded.count));
}

static uint8_t nal_type(const uint8_t *access_unit)
{
	/* The access units of the encoder start with a 4-byte start code */
	zassert_mem_equal(access_unit, ((uint8_t[]){0, 0, 0, 1}), 4, "no start code");

	return access_unit[4] & 0x1F;
}

/* Every frame comes back with its size, its timestamp and its content */
static void round_trip(struct test_h264_fixture *fix, uint16_t width, uint16_t height)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fix->dec_pipeline;

	encode_all(fix, width, height, 0);

	/* A sequence parameter set opens the first access unit, a slice the others */
	zassert_equal(nal_type(encoded.data[0]), 7);
	for (uint8_t n = 1; n < TEST_FRAMES_NUM; n++) {
		zassert_equal(nal_type(encoded.data[n]), 1, "frame %u is not a P frame", n);
		zassert_true(encoded.pts[n] >= encoded.pts[n - 1], "pts went backwards at %u", n);
	}

	decoded.width = width;
	decoded.height = height;
	set_caps(&fix->dec_src, &fix->dec_sink, VIDEO_PIX_FMT_H264, VIDEO_PIX_FMT_YUV420, width,
		 height);
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));

	for (uint8_t n = 0; n < TEST_FRAMES_NUM; n++) {
		decoded.expected[n] = n;
		zassert_ok(mpipe_app_src_push(&fix->dec_src, encoded.data[n], encoded.size[n],
					      K_SECONDS(1)));
	}

	zassert_ok(mpipe_app_src_eos(&fix->dec_src, K_SECONDS(1)));
	expect_eos();
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY));

	zassert_equal(atomic_get(&decoded.count), TEST_FRAMES_NUM, "decoded %ld frames",
		      atomic_get(&decoded.count));
	zassert_equal(atomic_get(&key_frame_requests), 0);
	for (uint8_t n = 0; n < TEST_FRAMES_NUM; n++) {
		zassert_equal(decoded.size[n], TEST_FRAME_SIZE(width, height), "frame %u size %u",
			      n, decoded.size[n]);
		zassert_true(decoded.luma_error[n] <= TEST_MAX_LUMA_ERROR,
			     "frame %u differs from its source by %u per pixel", n,
			     decoded.luma_error[n]);
		if (n > 0U) {
			zassert_true(decoded.pts[n] >= decoded.pts[n - 1],
				     "pts went backwards at %u", n);
		}
	}
}

ZTEST_F(test_h264, test_round_trip)
{
	round_trip(fixture, TEST_MAX_WIDTH, TEST_MAX_HEIGHT);
}

/* A frame size that is not a multiple of the macroblock size is cropped by the decoder */
ZTEST_F(test_h264, test_round_trip_cropped)
{
	round_trip(fixture, 80, 56);
}

/*
 * A corrupted frame: the decoder asks for a key frame, shows nothing of the
 * frames that depend on the lost one, and resumes at the key frame.
 */
ZTEST_F(test_h264, test_key_frame_request)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fixture->dec_pipeline;
	const uint16_t width = TEST_MAX_WIDTH;
	const uint16_t height = TEST_MAX_HEIGHT;
	/* Frames 0 and 1 are shown, 2 is corrupted, 3 and 4 are skipped, 5 is a key frame */
	static const uint8_t shown[] = {0, 1, 5, 6, 7};

	encode_all(fixture, width, height, BIT(5));
	zassert_equal(nal_type(encoded.data[5]), 7, "frame 5 is not a key frame");

	/* Keep the slice header and ruin the slice data */
	memset(&encoded.data[2][8], 0xFF, encoded.size[2] - 8);

	decoded.width = width;
	decoded.height = height;
	memcpy(decoded.expected, shown, sizeof(shown));
	set_caps(&fixture->dec_src, &fixture->dec_sink, VIDEO_PIX_FMT_H264, VIDEO_PIX_FMT_YUV420,
		 width, height);
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));

	for (uint8_t n = 0; n < TEST_FRAMES_NUM; n++) {
		zassert_ok(mpipe_app_src_push(&fixture->dec_src, encoded.data[n], encoded.size[n],
					      K_SECONDS(1)));
	}

	zassert_ok(mpipe_app_src_eos(&fixture->dec_src, K_SECONDS(1)));
	expect_eos();
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY));

	zassert_equal(atomic_get(&key_frame_requests), 1, "%ld key frame requests",
		      atomic_get(&key_frame_requests));
	zassert_equal(atomic_get(&decoded.count), ARRAY_SIZE(shown), "decoded %ld frames",
		      atomic_get(&decoded.count));
	ARRAY_FOR_EACH(shown, n) {
		zassert_true(decoded.luma_error[n] <= TEST_MAX_LUMA_ERROR,
			     "frame %u differs from its source by %u per pixel", shown[n],
			     decoded.luma_error[n]);
	}
}

/*
 * A stream joined in its middle: the decoder asks for a key frame at the first
 * frame, shows nothing before the key frame and everything from it.
 */
ZTEST_F(test_h264, test_key_frame_request_at_start)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fixture->dec_pipeline;
	const uint16_t width = TEST_MAX_WIDTH;
	const uint16_t height = TEST_MAX_HEIGHT;
	/* Frame 0 is not given, 1 and 2 are skipped, 3 is a key frame */
	static const uint8_t shown[] = {3, 4, 5, 6, 7};

	encode_all(fixture, width, height, BIT(3));
	zassert_equal(nal_type(encoded.data[3]), 7, "frame 3 is not a key frame");

	decoded.width = width;
	decoded.height = height;
	memcpy(decoded.expected, shown, sizeof(shown));
	set_caps(&fixture->dec_src, &fixture->dec_sink, VIDEO_PIX_FMT_H264, VIDEO_PIX_FMT_YUV420,
		 width, height);
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));

	for (uint8_t n = 1; n < TEST_FRAMES_NUM; n++) {
		zassert_ok(mpipe_app_src_push(&fixture->dec_src, encoded.data[n], encoded.size[n],
					      K_SECONDS(1)));
	}

	zassert_ok(mpipe_app_src_eos(&fixture->dec_src, K_SECONDS(1)));
	expect_eos();
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY));

	zassert_equal(atomic_get(&key_frame_requests), 1, "%ld key frame requests",
		      atomic_get(&key_frame_requests));
	zassert_equal(atomic_get(&decoded.count), ARRAY_SIZE(shown), "decoded %ld frames",
		      atomic_get(&decoded.count));
	ARRAY_FOR_EACH(shown, n) {
		zassert_true(decoded.luma_error[n] <= TEST_MAX_LUMA_ERROR,
			     "frame %u differs from its source by %u per pixel", shown[n],
			     decoded.luma_error[n]);
	}
}
