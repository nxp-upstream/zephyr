/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mp/mp_dispatch.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_pad.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

struct mp_pad_api_fixture {
	struct mp_pad src_pad;
	struct mp_pad sink_pad;
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

	mp_pad_init(&fix->src_pad, 0, MP_PAD_SRC, MP_PAD_ALWAYS);
	mp_pad_init(&fix->sink_pad, 1, MP_PAD_SINK, MP_PAD_ALWAYS);

	zassert_equal(fix->src_pad.object.id, 0, "id != 0 after init");
	zassert_equal(fix->src_pad.direction, MP_PAD_SRC, "direction != SRC");
	zassert_equal(fix->sink_pad.direction, MP_PAD_SINK, "direction != SINK");
	zassert_equal(fix->src_pad.presence, MP_PAD_ALWAYS, "presence != ALWAYS");
	zassert_true(mp_structure_is_any(&fix->src_pad.caps), "caps not ANY after init");
	zassert_is_null(fix->src_pad.peer, "peer != NULL after init");
	zassert_equal(fix->src_pad.mode, MP_PAD_MODE_NONE, "mode != NONE after init");
}

static void pad_after(void *f)
{
	struct mp_pad_api_fixture *fix = f;

	mp_structure_clear(&fix->src_pad.caps);
	mp_structure_clear(&fix->sink_pad.caps);
}

ZTEST_SUITE(mp_pad_api, NULL, pad_suite_setup, pad_before, pad_after, NULL);

ZTEST_F(mp_pad_api, test_link_sets_peers)
{
	zassert_ok(mp_pad_link(&fixture->src_pad, &fixture->sink_pad), "mp_pad_link failed");
	zassert_equal(fixture->src_pad.peer, &fixture->sink_pad, "src_pad peer != sink_pad");
	zassert_equal(fixture->sink_pad.peer, &fixture->src_pad, "sink_pad peer != src_pad");
}

ZTEST_F(mp_pad_api, test_sanity)
{
	zassert_true(mp_pad_link(NULL, &fixture->sink_pad) < 0, "link(NULL, sink) did not fail");
	zassert_true(mp_pad_link(&fixture->src_pad, NULL) < 0, "link(src, NULL) did not fail");
	zassert_true(mp_pad_link(NULL, NULL) < 0, "link(NULL, NULL) did not fail");

	struct mp_dispatch evt;

	mp_dispatch_eos_init(&evt);

	zassert_true(mp_pad_send_event(NULL, &evt) < 0, "send_event(NULL, evt) did not fail");
	zassert_true(mp_pad_send_event(&fixture->src_pad, NULL) < 0,
		     "send_event(pad, NULL) did not fail");

	mp_dispatch_clear(&evt);

	struct mp_dispatch q;

	mp_dispatch_caps_init(&q, NULL);

	zassert_true(mp_pad_query(NULL, &q) < 0, "query(NULL, q) did not fail");
	zassert_true(mp_pad_query(&fixture->src_pad, NULL) < 0, "query(pad, NULL) did not fail");

	fixture->src_pad.queryfn = NULL;
	zassert_true(mp_pad_query(&fixture->src_pad, &q) < 0, "query(no queryfn) did not fail");

	mp_dispatch_clear(&q);
}

/* Reports two capabilities, distinguished by their bit width */
static int fake_enum_caps(struct mp_pad *pad, uint32_t index, const struct mp_structure *filter,
			  struct mp_structure *out)
{
	static const uint32_t widths[] = {16U, 24U};
	struct mp_structure candidate;
	struct mp_value value;
	int ret;

	ARG_UNUSED(pad);

	if (index >= ARRAY_SIZE(widths)) {
		return -ENOENT;
	}

	mp_structure_init(&candidate, MP_MEDIA_AUDIO_PCM);
	value.type = MP_TYPE_UINT;
	value.v_uint = widths[index];
	zassert_ok(mp_structure_append_value(&candidate, MP_CAPS_BITWIDTH, &value),
		   "append failed");

	if (filter == NULL) {
		*out = candidate;
		return 0;
	}

	ret = mp_structure_intersect(&candidate, filter, out);

	return (ret != 0) ? -EAGAIN : 0;
}

ZTEST_F(mp_pad_api, test_enum_caps)
{
	struct mp_structure out;
	struct mp_structure filter;
	struct mp_value bitwidth = {.type = MP_TYPE_UINT};

	/* The default produces the pad's own capability, which is ANY here */
	zassert_ok(mp_pad_enum_caps(&fixture->src_pad, 0, NULL, &out), "default enum failed");
	zassert_true(mp_structure_is_any(&out), "ANY caps did not enumerate an ANY structure");
	zassert_equal(mp_pad_enum_caps(&fixture->src_pad, 1, NULL, &out), -ENOENT,
		      "ANY caps enumerated more than once");

	zassert_equal(mp_pad_enum_caps(NULL, 0, NULL, &out), -EINVAL, "enum(NULL pad) != -EINVAL");

	fixture->src_pad.enum_capsfn = fake_enum_caps;

	zassert_ok(mp_pad_enum_caps(&fixture->src_pad, 1, NULL, &out), "enum 1 failed");
	zassert_equal(mp_value_get_uint(mp_structure_get_value(&out, MP_CAPS_BITWIDTH)), 24U,
		      "wrong capability at index 1");

	/* A filter only the second capability satisfies */
	zassert_ok(mp_structure_init(&filter, MP_MEDIA_AUDIO_PCM), "filter init failed");
	bitwidth.v_uint = 24U;
	zassert_ok(mp_structure_append_value(&filter, MP_CAPS_BITWIDTH, &bitwidth),
		   "filter append failed");

	zassert_equal(mp_pad_enum_caps(&fixture->src_pad, 0, &filter, &out), -EAGAIN,
		      "unmatched index != -EAGAIN");
	zassert_ok(mp_pad_enum_caps(&fixture->src_pad, 1, &filter, &out), "matching 1 failed");

	/* The search skips what does not match and stops at what does */
	zassert_ok(mp_pad_enum_first(&fixture->src_pad, &filter, &out), "enum_first failed");
	zassert_equal(mp_value_get_uint(mp_structure_get_value(&out, MP_CAPS_BITWIDTH)), 24U,
		      "enum_first picked the wrong capability");

	/* No filter means the first capability wins */
	zassert_ok(mp_pad_enum_first(&fixture->src_pad, NULL, &out), "enum_first(NULL) failed");
	zassert_equal(mp_value_get_uint(mp_structure_get_value(&out, MP_CAPS_BITWIDTH)), 16U,
		      "enum_first(NULL) did not pick index 0");

	/* A filter nothing satisfies exhausts the enumeration */
	zassert_ok(mp_structure_init(&filter, MP_MEDIA_AUDIO_PCM), "filter re-init failed");
	bitwidth.v_uint = 32U;
	zassert_ok(mp_structure_append_value(&filter, MP_CAPS_BITWIDTH, &bitwidth),
		   "filter re-append failed");
	zassert_equal(mp_pad_enum_first(&fixture->src_pad, &filter, &out), -ENODATA,
		      "unsatisfiable filter != -ENODATA");

	fixture->src_pad.enum_capsfn = NULL;
	zassert_equal(mp_pad_enum_caps(&fixture->src_pad, 0, NULL, &out), -EINVAL,
		      "enum without a hook != -EINVAL");
}
