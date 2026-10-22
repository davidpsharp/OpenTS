/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// OPENTS_VQALOG=1 diagnostics for a movie that stops the game. The game thread notes where
// it is about to go (Mark); a watchdog thread started by the map-select wait reports the
// last note once a second, so a stall shows the call it is stuck in.

#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace VQLog {

inline bool Enabled(void)
{
	static bool const on = getenv("OPENTS_VQALOG") != nullptr && getenv("OPENTS_VQALOG")[0] == '1';
	return(on);
}

inline std::atomic<char const *> & Where(void)
{
	static std::atomic<char const *> where{"idle"};
	return(where);
}

inline std::atomic<unsigned> & Seq(void)
{
	static std::atomic<unsigned> seq{0};
	return(seq);
}

// Calls of the SDL audio feed callback, for the watchdog.
inline std::atomic<unsigned> & Feeds(void)
{
	static std::atomic<unsigned> feeds{0};
	return(feeds);
}

inline void Mark(char const * where)
{
	if (Enabled()) {
		Where().store(where, std::memory_order_relaxed);
		Seq().fetch_add(1, std::memory_order_relaxed);
	}
}

// Counts one turn of a loop inside the movie player. With OPENTS_VQALOG=1 every SPIN_REPORT
// turns it writes a line straight to stderr with write(2), from the calling thread, so a loop
// that never ends shows even when no other thread gets to run. Returns true once the loop
// has turned more than limit times, and the caller must then leave it.
unsigned const SPIN_REPORT = 20000;

inline bool Spin(char const * name, unsigned & count, unsigned limit)
{
	count++;
	if (Enabled() && count % SPIN_REPORT == 0) {
		char line[96];
		int length = snprintf(line, sizeof(line), "VQALOG-LOOP: %s turn %u\n", name, count);
#ifndef _WIN32
		if (length > 0) {
			ssize_t ignored = write(2, line, (size_t)length);
			(void)ignored;
		}
#else
		fputs(line, stderr);
#endif
	}
	if (count > limit) {
		if (count == limit + 1) {
			fprintf(stderr, "VQALOG: %s left after %u turns\n", name, limit);
		}
		return(true);
	}
	return(false);
}

}
