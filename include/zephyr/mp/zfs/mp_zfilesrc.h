/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief zfilesrc: file source element
 */

#ifndef __MP_ZFILESRC_H__
#define __MP_ZFILESRC_H__

#include <zephyr/fs/fs.h>

#include <zephyr/mp/core/mp_property.h>
#include <zephyr/mp/core/mp_src.h>

#define MP_ZFILESRC(self) ((struct mp_zfilesrc *)self)

/**
 * @brief zfilesrc property identifiers
 *
 * Extends base source properties.
 */
enum prop_zfilesrc {
	/** Path to file (const char *) */
	PROP_ZFILESRC_PATH = PROP_SRC_LAST + 1,
	/** Read block size in bytes (uint32_t*) */
	PROP_ZFILESRC_BLOCKSIZE,
};

struct mp_zfilesrc {
	/** Base source */
	struct mp_src src;
	/** Buffer pool used by this source */
	struct mp_buffer_pool pool;
	/** Downstream-proposed buffer pool (may be NULL) */
	struct mp_buffer_pool *downstream_pool;
	/** File handle */
	struct fs_file_t file;
	bool file_open;
	/** File path */
	const char *path;
	/** Read chunk size */
	uint32_t blocksize;
};

void mp_zfilesrc_init(struct mp_element *self);

#endif /* __MP_ZFILESRC_H__ */
