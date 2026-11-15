/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_mpipe_pipeline

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_bin.h>
#include <zephyr/mpipe/mpipe_dt.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pipeline.h>

LOG_MODULE_REGISTER(mpipe_dt, CONFIG_MPIPE_LOG_LEVEL);

struct mpipe_dt_pipeline {
	struct mpipe *pipeline;
#if defined(CONFIG_MPIPE_PLAYER)
	struct mpipe_player *player;
#endif
	const struct mpipe_dt_element *const *elements;
	const char *name;
	int thread_priority;
	uint8_t num_elements;
	uint8_t id;
	bool has_thread_priority;
	bool autostart;
};

/* A pipeline and each queue streams on a thread of the framework's pool */
#define MPIPE_DT_THREADS_NUM                                                                       \
	(DT_NUM_INST_STATUS_OKAY(zephyr_mpipe_pipeline) +                                          \
	 DT_NUM_INST_STATUS_OKAY(zephyr_mpipe_queue))

BUILD_ASSERT(MPIPE_DT_THREADS_NUM <= CONFIG_MPIPE_THREADS_NUM,
	     "CONFIG_MPIPE_THREADS_NUM is below the pipelines and queues of the devicetree");

#if defined(CONFIG_MPIPE_PLAYER)
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(zephyr_mpipe_pipeline) <= CONFIG_MPIPE_PLAYER_NUM,
	     "CONFIG_MPIPE_PLAYER_NUM is below the pipelines of the devicetree");

#define MPIPE_DT_PLAYER_DEFINE(inst) struct mpipe_player Z_MPIPE_DT_PLAYER_NAME(DT_DRV_INST(inst));
#define MPIPE_DT_PLAYER_ENTRY(inst)  .player = &Z_MPIPE_DT_PLAYER_NAME(DT_DRV_INST(inst)),
#define MPIPE_DT_AUTOSTART_CHECK(inst)
#else
#define MPIPE_DT_PLAYER_DEFINE(inst)
#define MPIPE_DT_PLAYER_ENTRY(inst)
#define MPIPE_DT_AUTOSTART_CHECK(inst)                                                             \
	BUILD_ASSERT(!DT_INST_PROP(inst, autostart), "autostart needs CONFIG_MPIPE_PLAYER");
#endif

#define MPIPE_DT_ELEMENT_REF(node_id) &Z_MPIPE_DT_ELEMENT_NAME(node_id),

#define MPIPE_DT_PIPELINE_DEFINE(inst)                                                             \
	struct mpipe Z_MPIPE_DT_PIPELINE_NAME(DT_DRV_INST(inst));                                  \
	MPIPE_DT_PLAYER_DEFINE(inst)                                                               \
	MPIPE_DT_AUTOSTART_CHECK(inst)                                                             \
	static const struct mpipe_dt_element *const mpipe_dt_elements_##inst[] = {                 \
		DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, MPIPE_DT_ELEMENT_REF)};                    \
	BUILD_ASSERT(ARRAY_SIZE(mpipe_dt_elements_##inst) <= CONFIG_MPIPE_BIN_MAX_CHILDREN,        \
		     "CONFIG_MPIPE_BIN_MAX_CHILDREN is below the elements of a pipeline node");

DT_INST_FOREACH_STATUS_OKAY(MPIPE_DT_PIPELINE_DEFINE)

#define MPIPE_DT_PIPELINE_ENTRY(inst)                                                              \
	{                                                                                          \
		.pipeline = &Z_MPIPE_DT_PIPELINE_NAME(DT_DRV_INST(inst)),                          \
		MPIPE_DT_PLAYER_ENTRY(inst)                                                        \
		.elements = mpipe_dt_elements_##inst,                                              \
		.name = DT_NODE_FULL_NAME(DT_DRV_INST(inst)),                                      \
		.thread_priority = (int)DT_INST_PROP_OR(inst, thread_priority, 0),                 \
		.num_elements = ARRAY_SIZE(mpipe_dt_elements_##inst),                              \
		.id = inst,                                                                        \
		.has_thread_priority = DT_INST_NODE_HAS_PROP(inst, thread_priority),               \
		.autostart = DT_INST_PROP(inst, autostart),                                        \
	},

static int mpipe_dt_pipeline_build(const struct mpipe_dt_pipeline *dt_pipeline)
{
	struct mpipe_bin *bin = (struct mpipe_bin *)dt_pipeline->pipeline;
	int ret = mpipe_pipeline_init(dt_pipeline->pipeline, dt_pipeline->id);

	if (ret != 0) {
		LOG_ERR("%s: failed to initialize the pipeline (%d)", dt_pipeline->name, ret);
		return ret;
	}

	if (dt_pipeline->has_thread_priority) {
		ret = mpipe_object_set_properties(
			&bin->element.object, MPIPE_PROP_PIPELINE_THREAD_PRIORITY,
			&dt_pipeline->thread_priority, MPIPE_PROP_LIST_END);
		if (ret != 0) {
			LOG_ERR("%s: failed to set the thread priority (%d)", dt_pipeline->name,
				ret);
			return ret;
		}
	}

	for (uint8_t i = 0; i < dt_pipeline->num_elements; i++) {
		const struct mpipe_dt_element *dt = dt_pipeline->elements[i];

		ret = dt->init(dt);
		if (ret != 0) {
			LOG_ERR("%s: failed to initialize %s (%d)", dt_pipeline->name, dt->name,
				ret);
			return ret;
		}

		mpipe_element_set_name(dt->element, dt->name);

		ret = mpipe_bin_add(bin, dt->element, NULL);
		if (ret != 0) {
			LOG_ERR("%s: failed to add %s (%d)", dt_pipeline->name, dt->name, ret);
			return ret;
		}
	}

	/* In node order, so that the branches of a tee take its pads in that order */
	for (uint8_t i = 0; i < dt_pipeline->num_elements; i++) {
		const struct mpipe_dt_element *dt = dt_pipeline->elements[i];

		if (dt->source == NULL) {
			continue;
		}

		ret = mpipe_element_link_pads(dt->source->element, MPIPE_PAD_ID_ANY, dt->element,
					      MPIPE_PAD_ID_ANY);
		if (ret != 0) {
			LOG_ERR("%s: failed to link %s to %s (%d)", dt_pipeline->name,
				dt->source->name, dt->name, ret);
			return ret;
		}
	}

	LOG_INF("%s: %u elements linked", dt_pipeline->name, dt_pipeline->num_elements);

	return 0;
}

static int mpipe_dt_init(void)
{
	static const struct mpipe_dt_pipeline pipelines[] = {
		DT_INST_FOREACH_STATUS_OKAY(MPIPE_DT_PIPELINE_ENTRY)};

	ARRAY_FOR_EACH_PTR(pipelines, dt_pipeline) {
		int ret = mpipe_dt_pipeline_build(dt_pipeline);

		/* A pipeline that fails does not prevent the others */
		if (ret != 0) {
			continue;
		}

#if defined(CONFIG_MPIPE_PLAYER)
		ret = mpipe_player_init(dt_pipeline->player, dt_pipeline->pipeline);
		if (ret != 0) {
			LOG_ERR("%s: failed to initialize the player (%d)", dt_pipeline->name, ret);
			continue;
		}

		if (dt_pipeline->autostart) {
			ret = mpipe_player_play(dt_pipeline->player);
			if (ret != 0) {
				LOG_ERR("%s: failed to play (%d)", dt_pipeline->name, ret);
			}
		}
#endif
	}

	return 0;
}

SYS_INIT(mpipe_dt_init, APPLICATION, CONFIG_MPIPE_DT_INIT_PRIORITY);
