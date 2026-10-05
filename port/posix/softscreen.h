/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The screen the POSIX build draws into, in place of a GPU back buffer: the game frame is
** scaled into it, the UI overlay is drawn over it, and Backend_End_Frame copies it to the
** window. Pixels are 32 bits with the rows top first: red at bit RedShift and blue at bit
** BlueShift (0xXXRRGGBB, or 0xXXBBGGRR where the window wants that, as RISC OS's 32-bit modes
** do), green always at bit 8.
*/
#pragma once

#include <cstdint>

struct SoftScreen
{
	std::uint32_t * Pixels = nullptr;
	int Width = 0;
	int Height = 0;
	int Pitch = 0; // in pixels
	int RedShift = 16;
	int BlueShift = 0;
};

// The screen for this frame, or one with no pixels before Backend_Init.
SoftScreen const & Soft_Screen(void);
