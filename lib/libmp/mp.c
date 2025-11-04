/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>

#include <src/core/mp_element_factory.h>
#include <src/core/mp_pipeline.h>
#include <src/core/mp_plugin.h>

/**
 * @brief Initialize Media Pipe
 *
 * - Registering built-in elements
 * - Loading all enabled plugins
 */
int mp_init(void)
{
	/* Built-in elements */
	MP_ELEMENTFACTORY_DEFINE(pipeline, sizeof(MpPipeline), mp_pipeline_init);

	/* Plugins */
	initialize_plugins();

	return 0;
}

SYS_INIT(mp_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
