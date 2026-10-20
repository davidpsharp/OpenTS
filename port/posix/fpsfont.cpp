/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The digits of the frame rate counter: a 5x7 bitmap font, drawn at a whole number of pixels per
** font pixel, bright green (0,255,0) with a black outline so it can be read on any background.
** Self-contained, so a test can draw it into a memory buffer.
*/

#include "fpscounter.h"

#include <algorithm>

namespace
{

constexpr int GlyphWidth = 5;
constexpr int GlyphHeight = 7;
constexpr int GlyphGap = 1; // font pixels between digits

// One row per byte, bit 4 (0x10) the leftmost pixel.
constexpr std::uint8_t Digits[10][GlyphHeight] = {
	{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
	{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
	{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
	{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
	{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
	{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
	{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
	{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
	{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
	{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
};

void Fill(std::uint32_t * pixels, int pitch, int width, int height, int x0, int y0, int x1, int y1, std::uint32_t colour)
{
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	x1 = std::min(x1, width);
	y1 = std::min(y1, height);
	for (int y = y0; y < y1; y++) {
		std::uint32_t * row = pixels + (size_t)y * (size_t)pitch;
		std::fill(row + x0, row + std::max(x0, x1), colour);
	}
}

} // namespace


int FPS_Font_Scale(int screenheight)
{
	return(screenheight <= 600 ? 1 : (screenheight < 1500 ? 2 : 3));
}


void FPS_Draw_Number(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift,
	int number, int unit, int right, int bottom)
{
	if (pixels == nullptr || unit < 1) {
		return;
	}
	number = std::max(0, std::min(number, 9999));
	int digits[4];
	int count = 0;
	do {
		digits[count++] = number % 10;
		number /= 10;
	} while (number > 0);

	// The outline is a ring of this many pixels round every lit font pixel.
	int const outline = (unit + 1) / 2;
	std::uint32_t const black = 0xFF000000u;
	std::uint32_t const green = 0xFF000000u | (0xFFu << 8);
	(void)redshift;
	(void)blueshift; // pure green is the same in either order

	int const textwidth = (count * GlyphWidth + (count - 1) * GlyphGap) * unit;
	int const left = right - textwidth;
	int const top = bottom - GlyphHeight * unit;

	for (int pass = 0; pass < 2; pass++) {
		int const grow = pass == 0 ? outline : 0;
		for (int index = 0; index < count; index++) {
			int const glyphx = left + index * (GlyphWidth + GlyphGap) * unit;
			std::uint8_t const * glyph = Digits[digits[count - 1 - index]];
			for (int row = 0; row < GlyphHeight; row++) {
				for (int column = 0; column < GlyphWidth; column++) {
					if ((glyph[row] & (0x10 >> column)) == 0) {
						continue;
					}
					int const x = glyphx + column * unit;
					int const y = top + row * unit;
					Fill(pixels, pitch, width, height, x - grow, y - grow, x + unit + grow, y + unit + grow, pass == 0 ? black : green);
				}
			}
		}
	}
}
