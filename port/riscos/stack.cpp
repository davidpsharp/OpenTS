/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "stackprime.h"

#include <cstdint>

// The touches go downwards one page apart, as a probing compiler would make them, so each
// one lands on the page next to those already mapped.
__attribute__((noinline)) void Stack_Prime(size_t bytes)
{
	volatile char * here = (volatile char *)__builtin_frame_address(0);
	size_t const page = 4096;
	for (size_t offset = page; offset <= bytes; offset += page) {
		here[-(intptr_t)offset] = 0;
	}
}
