/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The clock a movie's picture follows: the sound heard so far, in ticks, from
// what the mixer has taken of the sound track. While the sound stands still
// the clock runs on wall time, so a track that ends early or a device being
// recovered does not freeze the picture.

#pragma once

#include <cstdint>


class AudioMovieClockClass
{
	public:
		AudioMovieClockClass(void) { Reset(1, 1, 0, 60); }

		// blockframes is one pushed block; latencyframes what the device holds
		// beyond the mixer; tickrate the ticks per second the caller wants back.
		void Reset(unsigned rate, unsigned blockframes, unsigned latencyframes, unsigned tickrate)
		{
			Rate = rate > 0 ? rate : 1;
			BlockFrames = blockframes;
			Latency = latencyframes;
			TickRate = tickrate;
			LastPosition = 0;
			LastWall = 0;
			TickCount = 0;
			PauseAdjust = 0;
			Paused = false;
			Waiting = false;
			FirstWall = 0;
		}

		// consumed: frames the mixer has taken; repeated: blocks the player fed
		// twice while starved; wall: the caller's wall clock in ticks.
		unsigned long Ticks(uint32_t consumed, unsigned repeated, unsigned long wall)
		{
			if (Paused) {
				return(TickCount);
			}
			unsigned long t = wall - PauseAdjust;
			uint32_t behind = Latency + repeated * BlockFrames;
			uint32_t heard = consumed > behind ? consumed - behind : 0;

			if (heard > 0 && heard <= LastPosition) {
				if (t > LastWall) {
					TickCount += t - LastWall;
					LastWall = t;
				}
			} else if (heard == 0 && LastPosition == 0) {
				// Nothing heard yet. A device that goes on taking nothing (no sound driver
				// running behind it) would hold the movie at its first frame for good, so after
				// a second of waiting the wall clock carries the movie.
				if (!Waiting) {
					Waiting = true;
					FirstWall = t;
				}
				LastWall = t;
				TickCount = (t - FirstWall >= TickRate) ? t - FirstWall - TickRate : 0;
			} else {
				LastPosition = heard;
				LastWall = t;
				TickCount = (unsigned long)((uint64_t)heard * TickRate / Rate);
			}
			return(TickCount);
		}

		void Pause(void) { Paused = true; }

		// Picks the clock up where it stopped, so the pause is not counted.
		void Resume(unsigned long wall)
		{
			if (Paused) {
				PauseAdjust = wall - LastWall;
				Paused = false;
			}
		}

		bool Is_Paused(void) const { return(Paused); }
		unsigned long Pause_Adjust(void) const { return(PauseAdjust); }

	private:
		unsigned Rate;
		unsigned BlockFrames;
		unsigned Latency;
		unsigned TickRate;
		uint32_t LastPosition;
		unsigned long LastWall;
		unsigned long TickCount;
		unsigned long PauseAdjust;
		bool Paused;
		bool Waiting;
		unsigned long FirstWall;
};
