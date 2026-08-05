/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DSP_H__
#define __DSP_H__

#include <stdbool.h>

/*
 * Load the DSP image sections into DSP memory and release the DSP from reset.
 * Returns true if the DSP control device was ready and the core was started,
 * false otherwise (e.g. the DSP control device is not ready). Callers that
 * cannot proceed without the DSP (such as ztest cases) can check the return
 * value to fail cleanly instead of waiting for a core that never runs.
 */
bool dsp_start(void);

#endif

