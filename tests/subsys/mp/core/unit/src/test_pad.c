/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/core/mp_caps.h>
#include <zephyr/mp/core/mp_dispatch.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pad.h>

struct mp_pad_api_fixture {
	struct mp_pad src_pad;
	struct mp_pad sink_pad;
	struct mp_caps *any_caps;
};

static void *pad_suite_setup(void)
{
	static struct mp_pad_api_fixture fixture;

	return &fixture;
}

static void pad_before(void *f)
{
	struct mp_pad_api_fixture *fix = f;

	memset(&fix->src_pad, 0, sizeof(fix->src_pad));
	memset(&fix->sink_pad, 0, sizeof(fix->sink_pad));

	fix->any_caps = mp_caps_new_any();
	mp_pad_init(&fix->src_pad, 0, MP_PAD_SRC, MP_PAD_ALWAYS, fix->any_caps);
	mp_pad_init(&fix->sink_pad, 1, MP_PAD_SINK, MP_PAD_ALWAYS, fix->any_caps);

	zassert_equal(fix->src_pad.object.id, 0, "Pad ID shall be set by init");
	zassert_equal(fix->src_pad.direction, MP_PAD_SRC, "Src pad direction shall be SRC");
	zassert_equal(fix->sink_pad.direction, MP_PAD_SINK, "Sink pad direction shall be SINK");
	zassert_equal(fix->src_pad.presence, MP_PAD_ALWAYS, "Pad presence shall be ALWAYS");
	zassert_equal(fix->src_pad.caps, fix->any_caps, "Pad caps pointer shall be set by init");
	zassert_is_null(fix->src_pad.peer, "Pad peer shall be NULL after init");
	zassert_equal(fix->src_pad.mode, MP_PAD_MODE_NONE, "Pad mode shall be NONE after init");
}

static void pad_after(void *f)
{
	struct mp_pad_api_fixture *fix = f;

	if (fix->any_caps) {
		mp_caps_unref(fix->any_caps);
		fix->any_caps = NULL;
	}
}

ZTEST_SUITE(mp_pad_api, NULL, pad_suite_setup, pad_before, pad_after, NULL);

ZTEST_F(mp_pad_api, test_new)
{
	/* With caps */
	struct mp_pad *pad = mp_pad_new(5, MP_PAD_SINK, MP_PAD_SOMETIMES, fixture->any_caps);

	zassert_not_null(pad, "mp_pad_new shall return a valid pointer");
	zassert_equal(pad->object.id, 5, "Pad id shall be set");
	zassert_equal(pad->direction, MP_PAD_SINK, "Direction shall be SINK");
	zassert_equal(pad->presence, MP_PAD_SOMETIMES, "Presence shall be SOMETIMES");
	zassert_equal(pad->caps, fixture->any_caps, "Caps shall be set");
	k_free(pad);

	/* With NULL caps */
	struct mp_pad *pad_null_caps = mp_pad_new(0, MP_PAD_SRC, MP_PAD_ALWAYS, NULL);

	zassert_not_null(pad_null_caps, "mp_pad_new with NULL caps shall succeed");
	zassert_is_null(pad_null_caps->caps, "Caps shall be NULL when passed NULL");
	k_free(pad_null_caps);
}

ZTEST_F(mp_pad_api, test_link_sets_peers)
{
	zassert_ok(mp_pad_link(&fixture->src_pad, &fixture->sink_pad),
		   "Linking valid pads shall succeed");
	zassert_equal(fixture->src_pad.peer, &fixture->sink_pad, "srcpad peer shall be sinkpad");
	zassert_equal(fixture->sink_pad.peer, &fixture->src_pad, "sinkpad peer shall be srcpad");
}

ZTEST_F(mp_pad_api, test_sanity)
{
	/* mp_pad_link null arguments */
	zassert_true(mp_pad_link(NULL, &fixture->sink_pad) < 0,
		     "mp_pad_link(NULL, sink) shall return error");
	zassert_true(mp_pad_link(&fixture->src_pad, NULL) < 0,
		     "mp_pad_link(src, NULL) shall return error");
	zassert_true(mp_pad_link(NULL, NULL) < 0, "mp_pad_link(NULL, NULL) shall return error");

	/* mp_pad_send_event null arguments */
	struct mp_dispatch evt;

	mp_dispatch_eos_init(&evt);

	zassert_true(mp_pad_send_event(NULL, &evt) < 0,
		     "mp_pad_send_event(NULL, ...) shall return error");
	zassert_true(mp_pad_send_event(&fixture->src_pad, NULL) < 0,
		     "mp_pad_send_event(pad, NULL) shall return error");

	mp_dispatch_clear(&evt);

	/* mp_pad_query null arguments and no queryfn */
	struct mp_dispatch q;

	mp_dispatch_caps_init(&q, NULL);

	zassert_true(mp_pad_query(NULL, &q) < 0, "mp_pad_query(NULL, ...) shall return error");
	zassert_true(mp_pad_query(&fixture->src_pad, NULL) < 0,
		     "mp_pad_query(pad, NULL) shall return error");

	fixture->src_pad.queryfn = NULL;
	zassert_true(mp_pad_query(&fixture->src_pad, &q) < 0,
		     "mp_pad_query with no queryfn shall return error");

	mp_dispatch_clear(&q);
}
