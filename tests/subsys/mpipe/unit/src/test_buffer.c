/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Buffer pools: what a pool requires stays apart from what a negotiation
 * settled on, and a stop forgets the latter, so one run's demands are not the
 * next run's floor.
 *
 * Views: a buffer over a range of another one keeps its parent alive and
 * hands it back when it goes.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_structure.h>

struct mpipe_buffer_api_fixture {
	struct mpipe_buffer_pool pool;
};

static void *buffer_suite_setup(void)
{
	static struct mpipe_buffer_api_fixture fixture;

	return &fixture;
}

static void buffer_before(void *f)
{
	struct mpipe_buffer_api_fixture *fix = f;
	const struct mpipe_buffer_pool_config req = {
		.size = 1024,
		.align = 4,
		.min_buffers = 2,
		.max_buffers = 8,
	};

	memset(fix, 0, sizeof(*fix));

	mpipe_buffer_pool_init(&fix->pool);
	zassert_false(fix->pool.started, "pool.started != false after init");
	zassert_ok(mpipe_buffer_pool_set_req_config(&fix->pool, &req),
		   "stating the requirement failed");
}

ZTEST_SUITE(mpipe_buffer_api, NULL, buffer_suite_setup, buffer_before, NULL, NULL);

/* A pool with no hook has nothing to do, which is a state and not an error */
ZTEST_F(mpipe_buffer_api, test_a_pool_without_hooks_reports_enosys)
{
	struct mpipe_structure config;

	zassert_ok(mpipe_structure_init(&config, MPIPE_MEDIA_AUDIO_PCM), "config init failed");

	zassert_equal(mpipe_buffer_pool_configure(&fixture->pool, &config), -ENOSYS,
		      "configure without a hook did not report -ENOSYS");
	zassert_equal(mpipe_buffer_pool_start(&fixture->pool), -ENOSYS,
		      "start without a hook did not report -ENOSYS");
	zassert_equal(mpipe_buffer_pool_stop(&fixture->pool), -ENOSYS,
		      "stop without a hook did not report -ENOSYS");
}

/* The requirement is the floor a run starts from, so init seeds both configs */
ZTEST_F(mpipe_buffer_api, test_req_config_seeds_the_live_config)
{
	zassert_equal(fixture->pool.config.min_buffers, 2, "live config did not take the floor");
	zassert_equal(fixture->pool.config.align, 4, "live align did not take the floor");
	zassert_equal(fixture->pool.req_config.min_buffers, 2, "requirement not stored");
}

/*
 * The heart of it: a run's negotiated demands must not survive into the next
 * one. This is the JPEG parser's extra buffer, applied twice.
 */
ZTEST_F(mpipe_buffer_api, test_stop_forgets_what_the_run_negotiated)
{
	struct mpipe_buffer_pool_config negotiated;

	for (int run = 0; run < 3; run++) {
		/* Every run starts from the requirement, never from the last run */
		zassert_equal(fixture->pool.config.min_buffers, 2,
			      "run %d started from the previous run's demand", run);

		negotiated = fixture->pool.config;
		negotiated.min_buffers += 1;
		zassert_ok(mpipe_buffer_pool_set_config(&fixture->pool, &negotiated),
			   "applying the negotiated config failed");
		zassert_equal(fixture->pool.config.min_buffers, 3, "the demand did not reach it");

		zassert_ok(mpipe_buffer_pool_stop(&fixture->pool), "stop failed");
	}
}

/*
 * A pool can be proposed upstream, have a demand written into it, and then be
 * turned down without ever starting. It still has to forget the demand, which
 * is why the reset sits before the not-started early return.
 */
ZTEST_F(mpipe_buffer_api, test_stop_resets_a_pool_that_never_started)
{
	struct mpipe_buffer_pool_config negotiated = fixture->pool.config;

	negotiated.min_buffers = 7;
	zassert_ok(mpipe_buffer_pool_set_config(&fixture->pool, &negotiated), "set_config failed");
	zassert_equal(fixture->pool.config.min_buffers, 7, "the demand did not reach it");

	zassert_false(fixture->pool.started, "the pool should not be started");
	zassert_ok(mpipe_buffer_pool_stop(&fixture->pool), "stop of an unstarted pool failed");
	zassert_equal(fixture->pool.config.min_buffers, 2, "an unstarted pool kept the demand");
}

/* A started pool cannot be reconfigured under the elements already using it */
ZTEST_F(mpipe_buffer_api, test_a_started_pool_refuses_reconfiguration)
{
	struct mpipe_buffer_pool_config negotiated = fixture->pool.config;

	fixture->pool.started = true;
	negotiated.min_buffers = 5;

	zassert_equal(mpipe_buffer_pool_set_config(&fixture->pool, &negotiated), -EBUSY,
		      "a started pool accepted a new config");
	zassert_equal(mpipe_buffer_pool_set_req_config(&fixture->pool, &negotiated), -EBUSY,
		      "a started pool accepted a new requirement");
	zassert_equal(fixture->pool.config.min_buffers, 2, "the refused config was applied anyway");
}

static int refusing_set_config(struct mpipe_buffer_pool *pool,
			       const struct mpipe_buffer_pool_config *cfg)
{
	ARG_UNUSED(pool);
	ARG_UNUSED(cfg);

	return -ENOSPC;
}

static int clamping_set_config(struct mpipe_buffer_pool *pool,
			       const struct mpipe_buffer_pool_config *cfg)
{
	pool->config = *cfg;
	pool->config.min_buffers = MIN(cfg->min_buffers, 4);

	return 0;
}

/*
 * The demand reaches a pool only through its owner, which may accept it, clamp
 * it, or refuse. A refusal must leave the pool as it was: that is what lets an
 * adopter fall back to its own pool, as the JPEG parser does.
 */
ZTEST_F(mpipe_buffer_api, test_the_owner_decides_what_it_accepts)
{
	struct mpipe_buffer_pool_config negotiated = fixture->pool.config;

	negotiated.min_buffers = 6;

	fixture->pool.set_config = refusing_set_config;
	zassert_equal(mpipe_buffer_pool_set_config(&fixture->pool, &negotiated), -ENOSPC,
		      "the pool's refusal was not reported");
	zassert_equal(fixture->pool.config.min_buffers, 2, "a refused config was applied anyway");

	fixture->pool.set_config = clamping_set_config;
	zassert_ok(mpipe_buffer_pool_set_config(&fixture->pool, &negotiated), "set_config failed");
	zassert_equal(fixture->pool.config.min_buffers, 4, "the pool's clamp was not honored");

	/* Stopping still restores the requirement, whatever the owner accepted */
	zassert_ok(mpipe_buffer_pool_stop(&fixture->pool), "stop failed");
	zassert_equal(fixture->pool.config.min_buffers, 2, "stop did not restore the requirement");
}

/* A parent for the views: one buffer, so its return to the pool is observable */
NET_BUF_POOL_FIXED_DEFINE(view_parent_pool, 1, 256, sizeof(struct mpipe_buffer_meta),
			  mpipe_buffer_destroy);

static struct net_buf *alloc_parent(uint32_t bytes_used)
{
	struct net_buf *parent = net_buf_alloc_len(&view_parent_pool, 256, K_NO_WAIT);
	struct mpipe_buffer_meta *m;

	zassert_not_null(parent, "parent allocation failed");

	m = mpipe_buffer_get_meta(parent);
	m->bytes_used = bytes_used;
	m->timestamp = 1234;
	parent->len = bytes_used;

	for (uint32_t i = 0; i < bytes_used; i++) {
		parent->data[i] = (uint8_t)i;
	}

	return parent;
}

/* A view is the parent's bytes, not a copy of them */
ZTEST(mpipe_buffer_api, test_a_view_covers_a_range_of_its_parent)
{
	struct net_buf *parent = alloc_parent(100);
	struct net_buf *view = mpipe_buffer_view(parent, 10, 50, K_NO_WAIT);
	const struct mpipe_buffer_meta *vm;

	zassert_not_null(view, "view creation failed");
	zassert_equal_ptr(view->data, parent->data + 10, "the view does not point into its parent");
	zassert_equal(view->len, 50, "view len is not the range length");

	vm = mpipe_buffer_get_meta(view);
	zassert_equal(vm->bytes_used, 50, "view bytes_used is not the range length");
	zassert_equal(vm->timestamp, 1234, "the parent's timestamp was not carried");
	zassert_is_null(vm->pool, "a view belongs to no pool");
	zassert_equal_ptr(vm->parent, parent, "the view does not record its parent");
	zassert_equal(view->data[0], 10, "the view does not read the parent's bytes");

	/* Writing through the view is writing the parent */
	view->data[1] = 0xAB;
	zassert_equal(parent->data[11], 0xAB, "the view is a copy, not a view");

	net_buf_unref(view);
	net_buf_unref(parent);
}

/* The parent lives as long as any view of it, whatever order they are released in */
ZTEST(mpipe_buffer_api, test_a_view_keeps_its_parent_alive)
{
	struct net_buf *parent = alloc_parent(100);
	struct net_buf *first = mpipe_buffer_view(parent, 0, 40, K_NO_WAIT);
	struct net_buf *second = mpipe_buffer_view(parent, 40, 60, K_NO_WAIT);

	zassert_not_null(first, "first view failed");
	zassert_not_null(second, "second view failed");
	zassert_equal(parent->ref, 3, "two views should hold two references");

	/* The owner lets go first: the parent must survive for the views */
	net_buf_unref(parent);
	zassert_is_null(net_buf_alloc_len(&view_parent_pool, 256, K_NO_WAIT),
			"the parent returned to its pool while views were alive");
	zassert_equal(second->data[0], 40, "the parent's bytes are gone");

	net_buf_unref(first);
	zassert_is_null(net_buf_alloc_len(&view_parent_pool, 256, K_NO_WAIT),
			"the parent returned to its pool while a view was alive");

	net_buf_unref(second);
	parent = net_buf_alloc_len(&view_parent_pool, 256, K_NO_WAIT);
	zassert_not_null(parent, "the parent did not return to its pool after the last view");
	net_buf_unref(parent);
}

/* A range outside the parent's valid bytes is refused, not clamped */
ZTEST(mpipe_buffer_api, test_a_view_must_fit_the_parent)
{
	struct net_buf *parent = alloc_parent(100);

	zassert_is_null(mpipe_buffer_view(parent, 90, 20, K_NO_WAIT), "a view past the end");
	zassert_is_null(mpipe_buffer_view(parent, 101, 0, K_NO_WAIT),
			"a view starting past the end");
	zassert_not_null(mpipe_buffer_view(parent, 100, 0, K_NO_WAIT), "an empty view at the end");
	zassert_equal(parent->ref, 2, "the refused views took references");

	/* Release the empty view through the parent's pool accounting */
	net_buf_unref(parent);
	zassert_is_null(net_buf_alloc_len(&view_parent_pool, 256, K_NO_WAIT),
			"the empty view does not hold its parent");
}
