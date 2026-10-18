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
**
** SDL's driver turned the hardware pointer on whenever it showed a cursor: each time the game
** changed its cursor (scrolling, the Options button's green one) and while it loaded (the
** cursors are made again for each map). So SDL is told to leave it alone (patches/
** sdl-riscos-own-pointer.patch, the SDL_RISCOS_HARDWARE_POINTER hint), and this file alone turns
** it on or off: off while the game has the screen, except for a cursor with no picture when the
** player chose the system pointer in Options.
*/

#include "riscospointer.h"
#include "riscosdesktop.h"

#include "always.h"
#include "goptions.h"

#include <kernel.h>

#include <algorithm>
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

// What the pointer was last drawn as, and where.
static PointerPicture const * _DrawnPicture = nullptr;
static float _DrawnX = -1.0f;
static float _DrawnY = -1.0f;

static PointerPicture const * Find_Picture(void)
{
	if (!SDL_CursorVisible()) {
		return(nullptr);
	}
	auto found = _Pictures.find(SDL_GetCursor());
	if (found == _Pictures.end()) {
		found = _Pictures.find(_Arrow);
	}
	return(found != _Pictures.end() ? &found->second : nullptr);
}

static void Note_Drawn(PointerPicture const * picture)
{
	_DrawnPicture = picture;
	SDL_GetMouseState(&_DrawnX, &_DrawnY);
}

extern GameOptionsClass Options;

// Whether the desktop's pointer is on, as this file last set it (-1: not known).
static int _HardwareOn = -1;

void RISCOS_Pointer_Init(void)
{
	SDL_SetHint("SDL_RISCOS_HARDWARE_POINTER", "0");
}

// Turns the desktop's pointer on or off to suit the game's cursor, unless the desktop has the
// screen (and then the desktop's pointer is its own).
static void Update_Hardware_Pointer(bool force)
{
	if (RISCOS_Desktop_Has_Screen()) {
		_HardwareOn = -1;
		return;
	}
	// The desktop's pointer only stands in for a cursor with no picture, and only if the player
	// asked for it: otherwise there's no pointer until the game's own arrow is ready.
	int const on = (Options.SystemCursor && Find_Picture() == nullptr && SDL_CursorVisible()) ? 1 : 0;
	if (force || on != _HardwareOn) {
		_kernel_osbyte(106, on, 0);
		_HardwareOn = on;
	}
}

void RISCOS_Pointer_Game_Takes_Screen(void)
{
	Update_Hardware_Pointer(true);
}

bool RISCOS_Pointer_Changed(void)
{
	PointerPicture const * const picture = Find_Picture();
	Update_Hardware_Pointer(false);
	float x = 0.0f;
	float y = 0.0f;
	SDL_GetMouseState(&x, &y);
	return(picture != _DrawnPicture || (_DrawnPicture != nullptr && (x != _DrawnX || y != _DrawnY)));
}

// The picture to draw now, or none (and then the desktop's pointer stays as SDL left it).
static PointerPicture const * Current_Picture(void)
{
	PointerPicture const * picture = Find_Picture();
	Note_Drawn(picture);
	Update_Hardware_Pointer(false);
	return(picture);
}

// 32-bit pixels, red at bit RedShift and blue at bit BlueShift.
struct Pixel32
{
	int RedShift, BlueShift;
	void Unpack(std::uint32_t pixel, unsigned & red, unsigned & green, unsigned & blue) const
	{
		red = (pixel >> RedShift) & 0xff;
		green = (pixel >> 8) & 0xff;
		blue = (pixel >> BlueShift) & 0xff;
	}
	std::uint32_t Pack(unsigned red, unsigned green, unsigned blue) const
	{
		return(0xff000000u | (red << RedShift) | (green << 8) | (blue << BlueShift));
	}
};

// Draws every step'th pixel of the picture with its hotspot at x, y.
template <typename PixelType, typename Format>
static void Draw_Picture(PointerPicture const & picture, int step, int atx, int aty,
	PixelType * pixels, int pitch, int width, int height, Format const & format)
{
	int const left = atx - picture.HotX / step;
	int const top = aty - picture.HotY / step;

	for (int y = 0; y * step < picture.Height; y++) {
		int const screeny = top + y;
		if (screeny < 0 || screeny >= height) {
			continue;
		}
		unsigned int const * source = &picture.Pixels[(size_t)(y * step) * (size_t)picture.Width];
		PixelType * dest = pixels + (size_t)screeny * (size_t)pitch;
		for (int x = 0; x * step < picture.Width; x++) {
			int const screenx = left + x;
			unsigned int const argb = source[x * step];
			unsigned int const alpha = argb >> 24;
			if (alpha == 0 || screenx < 0 || screenx >= width) {
				continue;
			}
			unsigned int red = (argb >> 16) & 0xff;
			unsigned int green = (argb >> 8) & 0xff;
			unsigned int blue = argb & 0xff;
			if (alpha < 255) {
				unsigned underred, undergreen, underblue;
				format.Unpack(dest[screenx], underred, undergreen, underblue);
				red = (red * alpha + underred * (255 - alpha)) / 255;
				green = (green * alpha + undergreen * (255 - alpha)) / 255;
				blue = (blue * alpha + underblue * (255 - alpha)) / 255;
			}
			dest[screenx] = format.Pack(red, green, blue);
		}
	}
}

void RISCOS_Pointer_Draw(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift)
{
	PointerPicture const * picture = pixels != nullptr ? Current_Picture() : nullptr;
	if (picture == nullptr) {
		return;
	}
	float mousex = 0.0f;
	float mousey = 0.0f;
	SDL_GetMouseState(&mousex, &mousey);
	Draw_Picture(*picture, 1, (int)mousex, (int)mousey, pixels, pitch, width, height, Pixel32{redshift, blueshift});
}

// Where the mouse is in a frame shown stretched over the screen rectangle given, and the step
// that makes a picture made at the screen's scale (a whole multiple of the frame's) the frame's.
static void Place_In_Frame(int width, int height, int destx, int desty, int destwidth, int destheight,
	int & x, int & y, int & step)
{
	float mousex = 0.0f;
	float mousey = 0.0f;
	SDL_GetMouseState(&mousex, &mousey);
	x = (int)((mousex - destx) * width / destwidth);
	y = (int)((mousey - desty) * height / destheight);
	int const scalex = (destwidth + width / 2) / width;
	int const scaley = (destheight + height / 2) / height;
	step = std::max(1, std::min(scalex, scaley));
}

void RISCOS_Pointer_Draw_Into_Frame(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift,
	int destx, int desty, int destwidth, int destheight)
{
	PointerPicture const * picture = pixels != nullptr && destwidth > 0 && destheight > 0 ? Current_Picture() : nullptr;
	if (picture == nullptr) {
		return;
	}
	int x, y, step;
	Place_In_Frame(width, height, destx, desty, destwidth, destheight, x, y, step);
	Draw_Picture(*picture, step, x, y, pixels, pitch, width, height, Pixel32{redshift, blueshift});
}

