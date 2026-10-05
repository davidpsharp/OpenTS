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
#include "ftimer.h"
#include "softscreen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

static SDL_Window * _Window = nullptr;
static SDL_Surface * _Screen = nullptr;
static SoftScreen _Soft;
static bool _Direct = false;	// _Soft is the window surface itself this frame

static int _FrameWidth = 0;
static int _FrameHeight = 0;
static std::vector<std::uint32_t> _Frame; // the last uploaded game frame, converted
static BackendScaleMode _ScaleMode = BACKEND_SCALE_NEAREST;

// For OPENTS_FPS: nanoseconds spent in each part of presenting, since the last report.
static Uint64 _TimeFrame = 0;		// converting and scaling the game frame
static Uint64 _TimeOverlay = 0;		// drawing the UI over it, between Present and End_Frame
static Uint64 _TimeBlit = 0;		// copying the screen to the window surface
static Uint64 _TimeUpdate = 0;		// handing the window surface to the system
static Uint64 _OverlayStart = 0;

// 565 to 0xFFRRGGBB, with the low bits filled from the high ones so white stays white.
static std::uint32_t _Expand565[65536];


static int _TableRedShift = -1;

static void Build_Expansion_Table(int redshift = 16, int blueshift = 0)
{
	if (_TableRedShift == redshift) {
		return;
	}
	_TableRedShift = redshift;
	for (unsigned value = 0; value < 65536; value++) {
		unsigned r = (value >> 11) & 0x1F;
		unsigned g = (value >> 5) & 0x3F;
		unsigned b = value & 0x1F;
		r = (r << 3) | (r >> 2);
		g = (g << 2) | (g >> 4);
		b = (b << 3) | (b >> 2);
		_Expand565[value] = 0xFF000000u | (r << redshift) | (g << 8) | (b << blueshift);
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

	int lastsy = -1;
	std::uint32_t const * lastrow = nullptr;
	for (int y = y0; y < y1; y++) {
		int const sy = (int)(((std::uint64_t)(y - desty) * stepy) >> 16);
		std::uint32_t const * source = &_Frame[(size_t)sy * (size_t)_FrameWidth];
		std::uint32_t * dest = _Soft.Pixels + (size_t)y * (size_t)_Soft.Pitch;
		// Scaling up repeats source rows; a repeat is a copy of the row above.
		if (sy == lastsy) {
			std::memcpy(dest + x0, lastrow + x0, (size_t)(x1 - x0) * 4);
			continue;
		}
		lastsy = sy;
		lastrow = dest;
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


static void Clear_Rect(int x, int y, int width, int height)
{
	int const x0 = std::max(x, 0), y0 = std::max(y, 0);
	int const x1 = std::min(x + width, _Soft.Width), y1 = std::min(y + height, _Soft.Height);
	for (int row = y0; row < y1; row++) {
		std::uint32_t * line = _Soft.Pixels + (size_t)row * (size_t)_Soft.Pitch;
		std::fill(line + x0, line + std::max(x0, x1), 0xFF000000u);
	}
}


static void Clear_Borders(int destx, int desty, int destwidth, int destheight)
{
	if (destwidth <= 0 || destheight <= 0) {
		Clear_Rect(0, 0, _Soft.Width, _Soft.Height);
		return;
	}
	Clear_Rect(0, 0, _Soft.Width, desty);
	Clear_Rect(0, desty + destheight, _Soft.Width, _Soft.Height - (desty + destheight));
	Clear_Rect(0, desty, destx, destheight);
	Clear_Rect(destx + destwidth, desty, _Soft.Width - (destx + destwidth), destheight);
}


bool Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode)
{
	if (_Screen == nullptr) {
		return(false);
	}
	Uint64 const start = SDL_GetTicksNS();

	// Where the window surface is 32-bit and the screen's size, draw straight into it, which
	// saves copying the whole screen every frame. Otherwise draw into our own and copy.
	_Direct = false;
	_Soft.Pixels = (std::uint32_t *)_Screen->pixels;
	_Soft.Pitch = _Screen->pitch / 4;
	_Soft.RedShift = 16;
	_Soft.BlueShift = 0;
	SDL_Surface * target = SDL_GetWindowSurface(_Window);
	if (target != nullptr && target->w == _Soft.Width && target->h == _Soft.Height && !SDL_MUSTLOCK(target)) {
		bool const rgb = target->format == SDL_PIXELFORMAT_XRGB8888 || target->format == SDL_PIXELFORMAT_ARGB8888;
		bool const bgr = target->format == SDL_PIXELFORMAT_XBGR8888 || target->format == SDL_PIXELFORMAT_ABGR8888;
		if (rgb || bgr) {
			_Soft.Pixels = (std::uint32_t *)target->pixels;
			_Soft.Pitch = target->pitch / 4;
			_Soft.RedShift = bgr ? 0 : 16;
			_Soft.BlueShift = bgr ? 16 : 0;
			_Direct = true;
		}
	}
	Build_Expansion_Table(_Soft.RedShift, _Soft.BlueShift);
	static bool reported = false;
	if (!reported) {
		reported = true;
		DebugString("Video: %s the window surface (%s, %dx%d)\n", _Direct ? "drawing straight into" : "copying to",
			target != nullptr ? SDL_GetPixelFormatName(target->format) : "none", target != nullptr ? target->w : 0, target != nullptr ? target->h : 0);
	}
	_ScaleMode = mode;
	if (pixels != nullptr) {
		Convert_Frame(pixels, pitch);
	}

	// The overlay is drawn over the frame each time, so the whole screen is redrawn: the
	// frame covers its rectangle, and only the borders around it need clearing.
	Clear_Borders(destx, desty, destwidth, destheight);
	Scale_Frame(destx, desty, destwidth, destheight);
	_OverlayStart = SDL_GetTicksNS();
	_TimeFrame += _OverlayStart - start;
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
	if (_OverlayStart != 0) {
		_TimeOverlay += SDL_GetTicksNS() - _OverlayStart;
		_OverlayStart = 0;
	}

	// OPENTS_SHOT=<file.bmp> saves the screen every 60 frames, for checking a run unseen.
	static char const * const shot = getenv("OPENTS_SHOT");
	static unsigned frames = 0;
	if (shot != nullptr && (++frames <= 10 || frames % 60 == 0)) {
		SDL_SaveBMP(_Direct ? SDL_GetWindowSurface(_Window) : _Screen, shot);
		DebugString("Video: %u frames presented\n", frames);
	}

	// OPENTS_FPS=1 logs, every five seconds, how many frames were presented and how many game
	// frames ran each second.
	static bool const fps = getenv("OPENTS_FPS") != nullptr;
	if (fps) {
		static Uint64 start = SDL_GetTicks();
		static unsigned count = 0;
		static int startframe = Frame;
		count++;
		Uint64 const now = SDL_GetTicks();
		if (now - start >= 5000) {
			double const seconds = (double)(now - start) / 1000.0;
			double const per = count > 0 ? 1.0e6 * count : 1.0;
			DebugString("Video: %.1f frames a second presented, %.1f game frames; ms a frame: scale %.2f, UI %.2f, blit %.2f, system %.2f\n",
				count / seconds, (Frame - startframe) / seconds, _TimeFrame / per, _TimeOverlay / per, _TimeBlit / per, _TimeUpdate / per);
			_TimeFrame = _TimeOverlay = _TimeBlit = _TimeUpdate = 0;
			start = now;
			count = 0;
			startframe = Frame;
		}
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
	Uint64 const blitstart = SDL_GetTicksNS();
	if (!_Direct) {
		SDL_BlitSurface(_Screen, nullptr, target, nullptr);
	}
	Uint64 const updatestart = SDL_GetTicksNS();
	SDL_UpdateWindowSurface(_Window);
	Uint64 const done = SDL_GetTicksNS();
	_TimeBlit += updatestart - blitstart;
	_TimeUpdate += done - updatestart;
}


char const * Backend_Renderer_Name(void)
{
	return("software");
}
