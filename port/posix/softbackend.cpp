/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The frame presenter for the POSIX build, in place of code/bgfxbackend.cpp. It draws in
** software, because RISC OS has no 3D drivers: the 565 game frame is converted and scaled
** into the soft screen, the UI overlay draws over that, and the result is copied to the
** window's SDL surface.
*/

#include "bgfxbackend.h"
#include "dbgprint.h"
#include "softscreen.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <vector>

static SDL_Window * _Window = nullptr;
static SDL_Surface * _Screen = nullptr;
static SoftScreen _Soft;

static int _FrameWidth = 0;
static int _FrameHeight = 0;
static std::vector<std::uint32_t> _Frame; // the last uploaded game frame, converted
static BackendScaleMode _ScaleMode = BACKEND_SCALE_NEAREST;

// 565 to 0xFFRRGGBB, with the low bits filled from the high ones so white stays white.
static std::uint32_t _Expand565[65536];


static void Build_Expansion_Table(void)
{
	for (unsigned value = 0; value < 65536; value++) {
		unsigned r = (value >> 11) & 0x1F;
		unsigned g = (value >> 5) & 0x3F;
		unsigned b = value & 0x1F;
		r = (r << 3) | (r >> 2);
		g = (g << 2) | (g >> 4);
		b = (b << 3) | (b >> 2);
		_Expand565[value] = 0xFF000000u | (r << 16) | (g << 8) | b;
	}
}


static bool Make_Screen(int width, int height)
{
	if (_Screen != nullptr) {
		SDL_DestroySurface(_Screen);
		_Screen = nullptr;
	}
	_Soft = SoftScreen();

	_Screen = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
	if (_Screen == nullptr) {
		DebugString("Video: no %dx%d screen: %s\n", width, height, SDL_GetError());
		return(false);
	}
	SDL_FillSurfaceRect(_Screen, nullptr, 0);

	_Soft.Pixels = (std::uint32_t *)_Screen->pixels;
	_Soft.Width = width;
	_Soft.Height = height;
	_Soft.Pitch = _Screen->pitch / 4;
	return(true);
}


SoftScreen const & Soft_Screen(void)
{
	return(_Soft);
}


void Backend_Build_Ortho_Projection(float * result, int width, int height)
{
	// Maps 0..width and 0..height, top first, to the -1..1 clip cube.
	std::memset(result, 0, 16 * sizeof(float));
	result[0] = 2.0f / (float)width;
	result[5] = -2.0f / (float)height;
	result[10] = 1.0f;
	result[12] = -1.0f;
	result[13] = 1.0f;
	result[15] = 1.0f;
}


bool Backend_Init(NativeWindow const & window, int drawablewidth, int drawableheight, BackendRenderer, bool)
{
	_Window = (SDL_Window *)window.Handle;
	if (_Window == nullptr) {
		return(false);
	}
	Build_Expansion_Table();
	return(Make_Screen(drawablewidth, drawableheight));
}


void Backend_Shutdown(void)
{
	if (_Screen != nullptr) {
		SDL_DestroySurface(_Screen);
		_Screen = nullptr;
	}
	_Soft = SoftScreen();
	_Frame.clear();
	_Window = nullptr;
}


bool Backend_Set_Frame_Size(int width, int height)
{
	if (width <= 0 || height <= 0) {
		return(false);
	}
	_FrameWidth = width;
	_FrameHeight = height;
	_Frame.assign((size_t)width * (size_t)height, 0xFF000000u);
	return(true);
}


void Backend_On_Resize(int drawablewidth, int drawableheight)
{
	if (drawablewidth > 0 && drawableheight > 0) {
		Make_Screen(drawablewidth, drawableheight);
	}
}


static void Convert_Frame(void const * pixels, int pitch)
{
	for (int y = 0; y < _FrameHeight; y++) {
		std::uint16_t const * source = (std::uint16_t const *)((char const *)pixels + (size_t)y * (size_t)pitch);
		std::uint32_t * dest = &_Frame[(size_t)y * (size_t)_FrameWidth];
		for (int x = 0; x < _FrameWidth; x++) {
			dest[x] = _Expand565[source[x]];
		}
	}
}


// Nearest-neighbour scaling into the destination rectangle, which is clipped to the screen.
static void Scale_Frame(int destx, int desty, int destwidth, int destheight)
{
	if (destwidth <= 0 || destheight <= 0 || _FrameWidth <= 0 || _FrameHeight <= 0) {
		return;
	}

	int const x0 = destx < 0 ? 0 : destx;
	int const y0 = desty < 0 ? 0 : desty;
	int const x1 = (destx + destwidth > _Soft.Width) ? _Soft.Width : destx + destwidth;
	int const y1 = (desty + destheight > _Soft.Height) ? _Soft.Height : desty + destheight;

	std::uint32_t const stepx = (std::uint32_t)(((std::uint64_t)_FrameWidth << 16) / (std::uint64_t)destwidth);
	std::uint32_t const stepy = (std::uint32_t)(((std::uint64_t)_FrameHeight << 16) / (std::uint64_t)destheight);

	for (int y = y0; y < y1; y++) {
		int const sy = (int)(((std::uint64_t)(y - desty) * stepy) >> 16);
		std::uint32_t const * source = &_Frame[(size_t)sy * (size_t)_FrameWidth];
		std::uint32_t * dest = _Soft.Pixels + (size_t)y * (size_t)_Soft.Pitch;
		if (destwidth == _FrameWidth) {
			std::memcpy(dest + x0, source + (x0 - destx), (size_t)(x1 - x0) * 4);
			continue;
		}
		std::uint32_t sx = (std::uint32_t)(x0 - destx) * stepx;
		for (int x = x0; x < x1; x++) {
			dest[x] = source[sx >> 16];
			sx += stepx;
		}
	}
}


bool Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode)
{
	if (_Soft.Pixels == nullptr) {
		return(false);
	}
	_ScaleMode = mode;
	if (pixels != nullptr) {
		Convert_Frame(pixels, pitch);
	}

	// The overlay is drawn over the frame each time, so the whole screen is redrawn.
	SDL_FillSurfaceRect(_Screen, nullptr, 0);
	Scale_Frame(destx, desty, destwidth, destheight);
	return(true);
}


bool Backend_Frame_Is_Point_Sampled(void)
{
	return(_ScaleMode != BACKEND_SCALE_LINEAR);
}


void Backend_End_Frame(void)
{
	if (_Window == nullptr || _Screen == nullptr) {
		return;
	}

	// OPENTS_SHOT=<file.bmp> saves the screen every 60 frames, for checking a run unseen.
	static char const * const shot = getenv("OPENTS_SHOT");
	static unsigned frames = 0;
	if (shot != nullptr && (++frames <= 10 || frames % 60 == 0)) {
		SDL_SaveBMP(_Screen, shot);
		DebugString("Video: %u frames presented\n", frames);
	}

	SDL_Surface * target = SDL_GetWindowSurface(_Window);
	if (target == nullptr) {
		static bool reported = false;
		if (!reported) {
			DebugString("Video: the window has no surface: %s\n", SDL_GetError());
			reported = true;
		}
		return;
	}
	SDL_BlitSurface(_Screen, nullptr, target, nullptr);
	SDL_UpdateWindowSurface(_Window);
}


char const * Backend_Renderer_Name(void)
{
	return("software");
}
