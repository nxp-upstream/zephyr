/*
 * Copyright 2023 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header for kw45 platform
 *
 * This header file is used to specify and describe board-level aspects for the
 * 'kw45' platform.
 */

#ifndef _SOC__H_
#define _SOC__H_

#include <fsl_port.h>

#define PORT_MUX_GPIO kPORT_MuxAsGpio

#ifndef _ASMLANGUAGE
#include <zephyr/sys/util.h>
#include <fsl_common.h>
#include "fsl_ccm32k.h"
#include "fsl_clock.h"

/* Add include for DTS generated information */
#include <zephyr/devicetree.h>

#endif /* !_ASMLANGUAGE */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SOC__H_ */
