/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TESTS_DRIVERS_GPIO_PCA_SERIES_PCAL6524_EMUL_H_
#define ZEPHYR_TESTS_DRIVERS_GPIO_PCA_SERIES_PCAL6524_EMUL_H_

#include <stddef.h>
#include <stdint.h>

struct emul;

int pcal6524_emul_get_reg(const struct emul *target, uint8_t reg, uint8_t *buf, size_t len);
int pcal6524_emul_set_reg(const struct emul *target, uint8_t reg, const uint8_t *buf, size_t len);

#endif /* ZEPHYR_TESTS_DRIVERS_GPIO_PCA_SERIES_PCAL6524_EMUL_H_ */
