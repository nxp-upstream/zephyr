/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Prepended to the decoder on Cortex-M4 and M7: the DSP intrinsics its SIMD
 * paths use, from the module's own header, which relies on the fixed-width
 * types without including them.
 */

#ifndef ZEPHYR_MODULES_JPEGDEC_JPEGDEC_CM_SIMD_H_
#define ZEPHYR_MODULES_JPEGDEC_JPEGDEC_CM_SIMD_H_

#include <stdint.h>

#include "my_cm4_simd.h"

#endif /* ZEPHYR_MODULES_JPEGDEC_JPEGDEC_CM_SIMD_H_ */
