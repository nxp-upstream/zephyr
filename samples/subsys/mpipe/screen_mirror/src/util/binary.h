/*
 * Copyright (C) 2018 Genymobile
 * Copyright (C) 2018-2025 Romain Vimont
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SC_BINARY_H
#define SC_BINARY_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static inline void sc_write16be(uint8_t *buf, uint16_t value)
{
	buf[0] = value >> 8;
	buf[1] = value;
}

static inline void sc_write32be(uint8_t *buf, uint32_t value)
{
	buf[0] = value >> 24;
	buf[1] = value >> 16;
	buf[2] = value >> 8;
	buf[3] = value;
}

static inline void sc_write64be(uint8_t *buf, uint64_t value)
{
	sc_write32be(buf, value >> 32);
	sc_write32be(&buf[4], (uint32_t)value);
}

static inline uint16_t sc_read16be(const uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
}

static inline uint32_t sc_read32be(const uint8_t *buf)
{
	return ((uint32_t)buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static inline uint64_t sc_read64be(const uint8_t *buf)
{
	uint32_t msb = sc_read32be(buf);
	uint32_t lsb = sc_read32be(&buf[4]);
	return ((uint64_t)msb << 32) | lsb;
}

/**
 * Convert a float between 0 and 1 to an unsigned 16-bit fixed-point value
 */
static inline uint16_t sc_float_to_u16fp(float f)
{
	assert(f >= 0.0f && f <= 1.0f);
	uint32_t u = f * 0x1p16f; // 2^16
	if (u >= 0xffff) {
		assert(u == 0x10000); // for f == 1.0f
		u = 0xffff;
	}
	return (uint16_t)u;
}

/**
 * Convert a float between -1 and 1 to a signed 16-bit fixed-point value
 */
static inline int16_t sc_float_to_i16fp(float f)
{
	assert(f >= -1.0f && f <= 1.0f);
	int32_t i = f * 0x1p15f; // 2^15
	assert(i >= -0x8000);
	if (i >= 0x7fff) {
		assert(i == 0x8000); // for f == 1.0f
		i = 0x7fff;
	}
	return (int16_t)i;
}

#endif