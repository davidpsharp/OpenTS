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
#include <cstdlib>

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

}
