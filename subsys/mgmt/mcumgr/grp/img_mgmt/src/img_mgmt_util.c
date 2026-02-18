/*
 * Copyright (c) 2018-2021 mcumgr authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/dfu/dfu_boot.h>

int img_mgmt_ver_str(const struct image_version *ver, char *dst)
{
	struct dfu_boot_img_version dfu_ver = {
		.major = ver->iv_major,
		.minor = ver->iv_minor,
		.revision = ver->iv_revision,
		.build_num = ver->iv_build_num,
	};

	return dfu_boot_ver_str(&dfu_ver, dst, IMG_MGMT_VER_MAX_STR_LEN);
}
