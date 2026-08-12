/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief File source element for the mpipe fs plugin.
 *
 * Reads data from a file using Zephyr's filesystem API and produces
 * buffers for downstream processing.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_FS_MPIPE_FILESRC_H_
#define ZEPHYR_INCLUDE_MPIPE_FS_MPIPE_FILESRC_H_

/**
 * @defgroup mpipe_fs fs
 * @ingroup mpipe_plugins
 * @brief File-based source and sink elements.
 */

/**
 * @defgroup mpipe_fs_sources Sources
 * @ingroup mpipe_fs
 * @brief File-backed source elements.
 * @{
 */

#include <zephyr/fs/fs.h>

#include <zephyr/mpipe/mpipe_src.h>

/**
 * @brief File source property identifiers.
 *
 * Extends the base source properties defined in @ref mpipe_prop_src.
 */
enum mpipe_prop_fs_src {
	/** Path to the input file (const char *). */
	MPIPE_PROP_FS_SRC_PATH = MPIPE_PROP_SRC_LAST,
	/** Read block size in bytes (uint32_t). */
	MPIPE_PROP_FS_SRC_BLOCKSIZE,
};

/**
 * @brief File source element.
 *
 * Extends the base @ref mpipe_src to read data from a file on any
 * Zephyr-supported filesystem.
 */
struct mpipe_filesrc {
	/** Base source element. */
	struct mpipe_src src;
	/** Buffer pool used by this source. */
	struct mpipe_buffer_pool pool;
	/** Downstream-proposed buffer pool (may be NULL). */
	struct mpipe_buffer_pool *downstream_pool;
	/** File handle. */
	struct fs_file_t file;
	/** Whether the file is currently open. */
	bool file_open;
	/** Path to the input file. */
	const char *path;
	/** Read chunk size in bytes. */
	uint32_t blocksize;
};

/**
 * @brief Initialize a file source element.
 *
 * @param self Pointer to the element to initialize.
 */
void mpipe_filesrc_init(struct mpipe_element *self);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_FS_MPIPE_FILESRC_H_ */
