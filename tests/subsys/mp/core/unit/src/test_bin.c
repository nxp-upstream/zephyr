/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/mp/core/mp_bin.h>
#include <zephyr/mp/core/mp_element.h>
#include <zephyr/mp/core/mp_pad.h>
#include <zephyr/mp/core/mp_pipeline.h>
#include <zephyr/mp/core/mp_src.h>
#include <zephyr/mp/core/mp_sink.h>

struct mp_bin_api_fixture {
	struct mp_bin bin;
	struct mp_pipeline pipeline;
	struct mp_sink sink;
	struct mp_src src;
};

static void *bin_suite_setup(void)
{
	static struct mp_bin_api_fixture fixture;

	return &fixture;
}

static void bin_before(void *f)
{
	struct mp_bin_api_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));
	MP_ELEMENT_INIT((struct mp_element *)&fix->bin, mp_bin_init, 0);
	MP_ELEMENT_INIT((struct mp_element *)&fix->src, mp_src_init, 1);
	MP_ELEMENT_INIT((struct mp_element *)&fix->sink, mp_sink_init, 2);

	zassert_equal(fix->bin.children_num, 0, "Bin shall have zero children after init");
	zassert_true(sys_dlist_is_empty(&fix->bin.children),
		     "Bin children list shall be empty after init");
	zassert_equal(((struct mp_element *)&fix->bin)->current_state, MP_STATE_READY,
		      "Bin element state shall be READY after init");
}

ZTEST_SUITE(mp_bin_api, NULL, bin_suite_setup, bin_before, NULL, NULL);

ZTEST_F(mp_bin_api, test_add_elements)
{
	/* Passing only NULL should add zero elements and succeed */
	zassert_ok(mp_bin_add(&fixture->bin, NULL), "Adding NULL does nothing and shall succeed");
	zassert_equal(fixture->bin.children_num, 0,
		      "No children shall be added when first arg is NULL");

	/* Add single element and verify count and container */
	zassert_ok(mp_bin_add(&fixture->bin, (struct mp_element *)&fixture->src, NULL),
		   "Adding a valid element shall succeed");
	zassert_equal(fixture->bin.children_num, 1, "Bin shall have one child");
	zassert_equal(fixture->src.element.object.container, (struct mp_object *)&fixture->bin,
		      "Child container shall reference the bin");

	/* Add a second element and verify order is preserved */
	zassert_ok(mp_bin_add(&fixture->bin, (struct mp_element *)&fixture->sink, NULL),
		   "Adding sink element shall succeed");
	zassert_equal(fixture->bin.children_num, 2, "Bin shall have two children");

	sys_dnode_t *first = sys_dlist_peek_head(&fixture->bin.children);

	zassert_not_null(first, "Children list shall not be empty");

	struct mp_element *first_elem = CONTAINER_OF(first, struct mp_element, object.node);

	zassert_equal(first_elem->object.id, 1, "First child shall be src element with id=1");

	/* Duplicate ID shall fail */
	struct mp_src dup_src;

	memset(&dup_src, 0, sizeof(dup_src));
	MP_ELEMENT_INIT((struct mp_element *)&dup_src, mp_src_init, 1); /* Same ID as fixture->src */

	zassert_true(mp_bin_add(&fixture->bin, (struct mp_element *)&dup_src, NULL) < 0,
		     "Adding element with duplicate ID shall fail");
	zassert_equal(fixture->bin.children_num, 2,
		      "Children count shall not increase on failed add");
}

ZTEST_F(mp_bin_api, test_add_varargs)
{
	zassert_ok(mp_bin_add(&fixture->bin, (struct mp_element *)&fixture->src, (struct mp_element *)&fixture->sink,
			      NULL),
		   "Adding multiple elements in one call shall succeed");
	zassert_equal(fixture->bin.children_num, 2,
		      "Bin shall have two children after variadic add");
}
