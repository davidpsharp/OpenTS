/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** RISC OS maps a stack a page at a time (ARMEABISupport), so a function whose frame is
** larger than a page faults unless it touches each page in turn, which clang does not do
** on 32-bit ARM. Stack_Prime touches the pages below the caller now, so that the stack is
** already mapped when such a function runs. It does nothing on other systems.
*/
#pragma once

#include <cstddef>

#ifdef __riscos__
void Stack_Prime(size_t bytes);
#else
inline void Stack_Prime(size_t) {}
#endif

// How much of each stack to map: UnixLib gives the main thread 1MB.
static size_t const STACK_PRIME_MAIN = 960 * 1024;
static size_t const STACK_PRIME_THREAD = 192 * 1024;
