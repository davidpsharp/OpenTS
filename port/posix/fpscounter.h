/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The frame rate counter (Shift+F8): the number of frames a second actually presented, small and bright
** green with a black outline in the bottom right corner of the screen. It is drawn only into the
** picture on its way to the display (softbackend.cpp), never into a game surface, so it can't
** change the game or its sync. Off, it costs one test of FPS_Counter_On per presented frame.
*/
#pragma once

#include <cstdint>

// Whether the counter is showing. Read it directly; change it with FPS_Counter_Toggle.
extern bool FPS_Counter_On;

// Shift+F8 pressed: shows or hides it (and logs which).
void FPS_Counter_Toggle(void);

// Call once for each frame that went to the display. Nothing happens while the counter is off.
void FPS_Counter_Presented(void);

// The rate measured so far, in frames a second.
int FPS_Counter_Value(void);

// Draws the counter into the 32-bit picture given (pitch in pixels; red at bit redshift, blue at
// bit blueshift, green at 8) for a display unit pixels per font pixel: see FPS_Font_Scale.
void FPS_Counter_Draw(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift, int unit);

// The size of one font pixel on a screen this many pixels tall: 1 up to 600, 2 up to 1500,
// then 3. A frame shown stretched by a factor (the overlay's) passes the scale divided by that.
int FPS_Font_Scale(int screenheight);

// Draws the whole number given, right aligned to the corner (right, bottom are the first pixel
// beyond it), clipped to the picture. For the counter and for the host-side test.
void FPS_Draw_Number(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift,
	int number, int unit, int right, int bottom);
