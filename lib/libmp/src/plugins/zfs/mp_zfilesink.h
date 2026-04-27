/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief zfilesink: file sink element
 */

#ifndef __MP_ZFILESINK_H__
#define __MP_ZFILESINK_H__

#include <zephyr/fs/fs.h>

#include <src/core/mp_property.h>
#include <src/core/mp_sink.h>

#define MP_ZFILESINK(self) ((struct mp_zfilesink *)self)

/**
 * @brief zfilesink property identifiers
 *
 * Extends base sink properties.
 */
enum prop_zfilesink {
	/** Path to file */
	PROP_ZFILESINK_PATH = PROP_SINK_LAST + 1,
};

struct mp_zfilesink {
	/** Base sink */
	struct mp_sink sink;
	/** File handle */
	struct fs_file_t file;
	bool file_open;
	/** File path */
	const char *path;
};

void mp_zfilesink_init(struct mp_element *self);

#endif /* __MP_ZFILESINK_H__ */
