/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include <zephyr/mp/mp.h>
#include <zephyr/mp/mp_fake_src.h>
#include <zephyr/mp/mp_sink.h>
#include <zephyr/mp/mp_transform.h>
#include <zephyr/mp/utils/mp_dump.h>

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	DUMP_PIPE_ID,
	DUMP_SRC_ID,
	DUMP_TRANSFORM_ID,
	DUMP_SINK_ID,
};

/* 'R', 'G', 'B', 'P': the fourcc VIDEO_PIX_FMT_RGB565 is spelled with */
#define TEST_FOURCC_RGBP 0x50424752U

#define TEST_DUMP_BUF_SIZE 2048

struct test_dump_capture {
	char buf[TEST_DUMP_BUF_SIZE];
	size_t len;
};

struct test_dump_fixture {
	struct mp_pipeline pipeline;
	struct mp_fake_src fake_src;
	struct mp_transform transform;
	struct mp_sink sink;
	struct test_dump_capture capture;
	struct mp_dump_sink sink_cfg;
};

/* A sink collecting the dump into a buffer, which is what lets a test read it */
static void test_dump_vprint(void *ctx, const char *fmt, va_list ap)
{
	struct test_dump_capture *cap = ctx;
	size_t room = sizeof(cap->buf) - cap->len;
	int written;

	if (room <= 1U) {
		return;
	}

	written = vsnprintk(&cap->buf[cap->len], room, fmt, ap);
	if (written <= 0) {
		return;
	}

	cap->len = MIN(cap->len + (size_t)written, sizeof(cap->buf) - 1U);
}

static void test_dump_reset(struct test_dump_fixture *fix)
{
	fix->capture.len = 0;
	fix->capture.buf[0] = '\0';
}

static void *dump_suite_setup(void)
{
	static struct test_dump_fixture fixture;

	return &fixture;
}

static void dump_before(void *f)
{
	struct test_dump_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));

	MP_ELEMENT_INIT(&fix->pipeline, mp_pipeline_init, DUMP_PIPE_ID);
	MP_ELEMENT_INIT(&fix->fake_src, mp_fake_src_init, DUMP_SRC_ID);
	MP_ELEMENT_INIT(&fix->transform, mp_transform_init, DUMP_TRANSFORM_ID);
	MP_ELEMENT_INIT(&fix->sink, mp_sink_init, DUMP_SINK_ID);

	fix->sink_cfg.vprint = test_dump_vprint;
	fix->sink_cfg.ctx = &fix->capture;

	zassert_ok(mp_bin_add((struct mp_bin *)&fix->pipeline, (struct mp_element *)&fix->fake_src,
			      (struct mp_element *)&fix->transform, (struct mp_element *)&fix->sink,
			      NULL),
		   "Failed to add elements");
}

ZTEST_SUITE(test_dump, NULL, dump_suite_setup, dump_before, NULL, NULL);

static void dump_link_chain(struct test_dump_fixture *fix)
{
	zassert_ok(mp_element_link((struct mp_element *)&fix->fake_src,
				   (struct mp_element *)&fix->transform,
				   (struct mp_element *)&fix->sink, NULL),
		   "Failed to link elements");
}

/* An element is named after its init function, with the prefix and suffix dropped */
ZTEST_F(test_dump, test_dump_names_every_element)
{
	dump_link_chain(fixture);

	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	zassert_not_null(strstr(fixture->capture.buf, "pipeline #0"), "Bin not named:\n%s",
			 fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "fake_src #1"), "Source not named:\n%s",
			 fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "transform #2"), "Transform not named:\n%s",
			 fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "sink #3"), "Sink not named:\n%s",
			 fixture->capture.buf);

	/*
	 * Neither the "mp_" prefix nor the "_init" suffix belongs in a name.
	 * Checked against the names themselves rather than the whole rendering,
	 * which legitimately carries "mp_" in the graph's own name.
	 */
	zassert_is_null(strstr(fixture->capture.buf, "mp_fake_src"), "Prefix not stripped:\n%s",
			fixture->capture.buf);
	zassert_is_null(strstr(fixture->capture.buf, "_init"), "Suffix not stripped:\n%s",
			fixture->capture.buf);
}

/* Every link becomes an edge between the two ports it actually joins */
ZTEST_F(test_dump, test_dump_draws_an_edge_per_link)
{
	dump_link_chain(fixture);

	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	zassert_not_null(strstr(fixture->capture.buf, "e0:src0 -> e1:sink0"),
			 "Edge from the source is missing or lands on the wrong pad:\n%s",
			 fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "e1:src1 -> e2:sink0"),
			 "Edge to the sink is missing or lands on the wrong pad:\n%s",
			 fixture->capture.buf);

	/* Every pad is linked, so nothing is marked */
	zassert_is_null(strstr(fixture->capture.buf, "#cc0000"),
			"A fully linked chain marked an element as unlinked:\n%s",
			fixture->capture.buf);
}

/* An element with a pad left over is marked, which is what a broken graph looks like */
ZTEST_F(test_dump, test_dump_marks_an_element_with_an_unlinked_pad)
{
	/* Deliberately left unlinked */
	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	zassert_not_null(strstr(fixture->capture.buf, "#cc0000"),
			 "An unlinked element was not marked:\n%s", fixture->capture.buf);
	zassert_is_null(strstr(fixture->capture.buf, " -> "),
			"Nothing is linked, so no edge should be drawn:\n%s", fixture->capture.buf);
}

/* The capability negotiated on each pad is what the dump exists to show */
ZTEST_F(test_dump, test_dump_shows_negotiated_caps)
{
	dump_link_chain(fixture);

	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	/* Nothing has negotiated yet, so every pad still constrains nothing */
	zassert_not_null(strstr(fixture->capture.buf, "<any>"),
			 "An un-negotiated pad should render as <any>:\n%s", fixture->capture.buf);

	zassert_equal(
		mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_PAUSED),
		MP_STATE_CHANGE_SUCCESS, "Pipeline failed to reach PAUSED");

	test_dump_reset(fixture);
	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	/* Every element, not just the bin, has to report the state it reached */
	zassert_not_null(strstr(fixture->capture.buf, "pipeline #0 PAUSED"),
			 "Bin state not shown:\n%s", fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "fake_src #1\\nPAUSED"),
			 "Source still reports its initial state:\n%s", fixture->capture.buf);
	zassert_is_null(strstr(fixture->capture.buf, "READY"),
			"An element was left behind at READY:\n%s", fixture->capture.buf);

	zassert_equal(mp_element_set_state((struct mp_element *)&fixture->pipeline, MP_STATE_READY),
		      MP_STATE_CHANGE_SUCCESS, "Pipeline failed to return to READY");
}

/* The rendering has to be a graph dot(1) can actually lay out */
ZTEST_F(test_dump, test_dump_is_a_well_formed_digraph)
{
	dump_link_chain(fixture);

	zassert_ok(mp_dump_bin((struct mp_bin *)&fixture->pipeline, &fixture->sink_cfg));

	zassert_equal(strncmp(fixture->capture.buf, "digraph mp_pipeline {", 21), 0,
		      "DOT does not open a digraph:\n%s", fixture->capture.buf);
	zassert_not_null(strstr(fixture->capture.buf, "rankdir=LR;"), "DOT lacks its layout:\n%s",
			 fixture->capture.buf);

	/* Every brace opened has to close, or dot(1) rejects the whole graph */
	int depth = 0;

	for (size_t i = 0; i < fixture->capture.len; i++) {
		if (fixture->capture.buf[i] == '{') {
			depth++;
		} else if (fixture->capture.buf[i] == '}') {
			depth--;
		}

		zassert_true(depth >= 0, "DOT closes a brace it never opened:\n%s",
			     fixture->capture.buf);
	}

	zassert_equal(depth, 0, "DOT leaves %d brace(s) open:\n%s", depth, fixture->capture.buf);
}

/* A capability renders its media type, its field names and a fourcc as characters */
ZTEST_F(test_dump, test_dump_caps_renders_fields_by_name)
{
	struct mp_structure caps;

	zassert_ok(mp_structure_init_fields(&caps, MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT,
					    MP_TYPE_UINT, TEST_FOURCC_RGBP, MP_CAPS_IMAGE_WIDTH,
					    MP_TYPE_UINT, 640, MP_CAPS_IMAGE_HEIGHT,
					    MP_TYPE_UINT_RANGE, 16, 480, 2, MP_CAPS_END));

	zassert_ok(mp_dump_caps(&caps, &fixture->sink_cfg));

	zassert_equal(strcmp(fixture->capture.buf,
			     "<video, format=RGBP, width=640, height=[16, 480, 2]>"),
		      0, "Unexpected rendering: '%s'", fixture->capture.buf);
}

/* An audio capability uses the same table, so its field names have to be there too */
ZTEST_F(test_dump, test_dump_caps_renders_audio_fields)
{
	struct mp_structure caps;

	zassert_ok(mp_structure_init_fields(&caps, MP_MEDIA_AUDIO_PCM, MP_CAPS_SAMPLE_RATE,
					    MP_TYPE_UINT, 48000, MP_CAPS_NUM_OF_CHANNEL,
					    MP_TYPE_UINT, 2, MP_CAPS_INTERLEAVED, MP_TYPE_BOOLEAN,
					    true, MP_CAPS_END));

	zassert_ok(mp_dump_caps(&caps, &fixture->sink_cfg));

	zassert_equal(strcmp(fixture->capture.buf,
			     "<audio/pcm, rate=48000, channels=2, interleaved=true>"),
		      0, "Unexpected rendering: '%s'", fixture->capture.buf);
}

/*
 * Constraining nothing because it intersects with everything is not the same as
 * constraining nothing because it intersects with nothing.
 */
ZTEST_F(test_dump, test_dump_caps_tells_any_from_empty)
{
	struct mp_structure caps;

	zassert_ok(mp_structure_init_any(&caps));
	zassert_ok(mp_dump_caps(&caps, &fixture->sink_cfg));
	zassert_equal(strcmp(fixture->capture.buf, "<any>"), 0, "Unexpected rendering: '%s'",
		      fixture->capture.buf);

	test_dump_reset(fixture);

	zassert_ok(mp_structure_init(&caps, MP_MEDIA_VIDEO));
	zassert_ok(mp_dump_caps(&caps, &fixture->sink_cfg));
	zassert_equal(strcmp(fixture->capture.buf, "<empty>"), 0, "Unexpected rendering: '%s'",
		      fixture->capture.buf);
}

ZTEST_F(test_dump, test_dump_rejects_bad_arguments)
{
	zassert_equal(mp_dump_bin(NULL, &fixture->sink_cfg), -EINVAL,
		      "A NULL bin should be rejected");
	zassert_equal(mp_dump_caps(NULL, &fixture->sink_cfg), -EINVAL,
		      "A NULL capability should be rejected");
}
