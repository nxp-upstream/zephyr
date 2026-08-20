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

#ifndef SC_COORDS
#define SC_COORDS

#include <stdint.h>

struct sc_size {
	uint16_t width;
	uint16_t height;
};

struct sc_point {
	int32_t x;
	int32_t y;
};

struct sc_position {
	// The video screen size may be different from the real device screen size,
	// so store to which size the absolute position apply, to scale it
	// accordingly.
	struct sc_size screen_size;
	struct sc_point point;
};

#endif