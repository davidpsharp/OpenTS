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

// The UI says it drew this frame.
void Soft_Note_UI_Drawn(void);

// Something else has drawn on the display (the desktop, while the game was away from it): the
// next frame is made and sent whole.
void Soft_Screen_Lost(void);

// What the UI drew, which the presenter lays over the game frame: the screen's size and pixel
// layout, premultiplied, with alpha at bit 24. Every pixel outside the box from X0,Y0 to
// X1,Y1 (X1 and Y1 excluded) is clear.
struct SoftLayer
{
	std::uint32_t * Pixels = nullptr;
	int Width = 0;
	int Height = 0;
	int Pitch = 0; // in pixels
	int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
	unsigned Version = 0; // changes whenever the pixels do
	// When PartValid, the pixels at version PartFrom were changed to these only in the box from
	// PartX0,PartY0 to PartX1,PartY1 (PartX1 and PartY1 excluded).
	bool PartValid = false;
	unsigned PartFrom = 0;
	int PartX0 = 0, PartY0 = 0, PartX1 = 0, PartY1 = 0;
};

SoftLayer const & Soft_UI_Layer(void);

// Ends the UI's frame; the layer then holds what the UI drew in it, or nothing if it drew nothing.
void Soft_UI_Finish_Frame(void);
