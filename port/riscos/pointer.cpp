/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The game's pointer on RISC OS. SDL's RISC OS driver makes no cursors: every SDL cursor turns
** on the desktop's pointer, so the game showed RISC OS's blue arrow over it. The hardware
** pointer has three colours and is at most 32 pixels wide, too little for the game's art, so
** the pointer is drawn into each frame instead, as the frame is redrawn in full every time,
** and the hardware pointer is kept off while it is.
*/

#include "riscospointer.h"

#include <kernel.h>

#include <map>
#include <vector>

struct PointerPicture
{
	std::vector<unsigned int> Pixels; // 0xAARRGGBB
	int Width = 0;
	int Height = 0;
	int HotX = 0;
	int HotY = 0;
};

static std::map<SDL_Cursor *, PointerPicture> _Pictures;
static SDL_Cursor * _Arrow = nullptr;

void RISCOS_Pointer_Add(SDL_Cursor * cursor, unsigned int const * pixels, int width, int height, int hotx, int hoty)
{
	if (cursor == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
		return;
	}
	PointerPicture & picture = _Pictures[cursor];
	picture.Pixels.assign(pixels, pixels + (size_t)width * (size_t)height);
	picture.Width = width;
	picture.Height = height;
	picture.HotX = hotx;
	picture.HotY = hoty;
}

void RISCOS_Pointer_Remove(SDL_Cursor * cursor)
{
	_Pictures.erase(cursor);
	if (_Arrow == cursor) {
		_Arrow = nullptr;
	}
}

void RISCOS_Pointer_Set_Arrow(SDL_Cursor * cursor)
{
	_Arrow = cursor;
}

void RISCOS_Pointer_Draw(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift)
{
	if (pixels == nullptr || !SDL_CursorVisible()) {
		return;
	}

	auto found = _Pictures.find(SDL_GetCursor());
	if (found == _Pictures.end()) {
		found = _Pictures.find(_Arrow);
	}
	if (found == _Pictures.end()) {
		return; // nothing to draw, so the desktop's pointer stays as SDL left it
	}

	// SDL turns the hardware pointer on whenever it shows a cursor; this one is drawn instead.
	_kernel_osbyte(106, 0, 0);

	float mousex = 0.0f;
	float mousey = 0.0f;
	SDL_GetMouseState(&mousex, &mousey);

	PointerPicture const & picture = found->second;
	int const left = (int)mousex - picture.HotX;
	int const top = (int)mousey - picture.HotY;

	for (int y = 0; y < picture.Height; y++) {
		int const screeny = top + y;
		if (screeny < 0 || screeny >= height) {
			continue;
		}
		unsigned int const * source = &picture.Pixels[(size_t)y * (size_t)picture.Width];
		std::uint32_t * dest = pixels + (size_t)screeny * (size_t)pitch;
		for (int x = 0; x < picture.Width; x++) {
			int const screenx = left + x;
			unsigned int const argb = source[x];
			unsigned int const alpha = argb >> 24;
			if (alpha == 0 || screenx < 0 || screenx >= width) {
				continue;
			}
			unsigned int red = (argb >> 16) & 0xff;
			unsigned int green = (argb >> 8) & 0xff;
			unsigned int blue = argb & 0xff;
			if (alpha < 255) {
				std::uint32_t const under = dest[screenx];
				red = (red * alpha + ((under >> redshift) & 0xff) * (255 - alpha)) / 255;
				green = (green * alpha + ((under >> 8) & 0xff) * (255 - alpha)) / 255;
				blue = (blue * alpha + ((under >> blueshift) & 0xff) * (255 - alpha)) / 255;
			}
			dest[screenx] = 0xff000000u | (red << redshift) | (green << 8) | (blue << blueshift);
		}
	}
}
