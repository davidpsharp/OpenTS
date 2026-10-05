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
** window. Pixels are 32-bit 0xAARRGGBB with the rows top first.
*/
#pragma once

#include <cstdint>

struct SoftScreen
{
	std::uint32_t * Pixels = nullptr;
	int Width = 0;
	int Height = 0;
	int Pitch = 0; // in pixels
};

// The screen for this frame, or one with no pixels before Backend_Init.
SoftScreen const & Soft_Screen(void);
