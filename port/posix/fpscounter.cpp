/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "fpscounter.h"

#include "dbgprint.h"

#include <chrono>
#include <cstdlib>

// OPENTS_FPSCOUNTER=1 starts with the counter on, for runs nobody can press Shift+F8 in.
bool FPS_Counter_On = getenv("OPENTS_FPSCOUNTER") != nullptr;

namespace
{

using Clock = std::chrono::steady_clock;

// The rate is taken over the last two half second periods, so it moves about every half second
// and is steady within it.
constexpr long long PeriodMs = 500;

Clock::time_point PeriodStart;
bool Started = false;
unsigned PeriodFrames = 0;
unsigned PreviousFrames = 0;
long long PreviousMs = 0;
int Value = 0;
Clock::time_point LastLogged;

} // namespace


int FPS_Counter_Value(void)
{
	return(Value);
}


void FPS_Counter_Toggle(void)
{
	FPS_Counter_On = !FPS_Counter_On;
	Started = false;
	Value = 0;
	DebugString("FPS counter %s\n", FPS_Counter_On ? "on" : "off");
}


void FPS_Counter_Presented(void)
{
	if (!FPS_Counter_On) {
		return;
	}
	Clock::time_point const now = Clock::now();
	if (!Started) {
		Started = true;
		PeriodStart = LastLogged = now;
		PeriodFrames = PreviousFrames = 0;
		PreviousMs = 0;
		return;
	}
	PeriodFrames++;
	long long const ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - PeriodStart).count();
	if (ms < PeriodMs) {
		return;
	}
	long long const totalms = ms + PreviousMs;
	Value = (int)(((long long)(PeriodFrames + PreviousFrames) * 1000 + totalms / 2) / totalms);
	PreviousFrames = PeriodFrames;
	PreviousMs = ms;
	PeriodFrames = 0;
	PeriodStart = now;
	if (now - LastLogged >= std::chrono::seconds(5)) {
		LastLogged = now;
		DebugString("FPS counter: %d frames a second presented\n", Value);
	}
}


void FPS_Counter_Draw(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift, int unit)
{
	int const margin = 2 * unit + (unit + 1) / 2;
	FPS_Draw_Number(pixels, pitch, width, height, redshift, blueshift, Value, unit, width - margin, height - margin);
}
