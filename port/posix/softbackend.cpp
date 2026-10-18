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
#include "riscosdesktop.h"
#include "riscosoverlay.h"
#include "riscospointer.h"
#include "softscreen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

void Emergency_Exit(void);

static SDL_Window * _Window = nullptr;
static SDL_Surface * _Screen = nullptr;
static SoftScreen _Soft;
static bool _Direct = false;	// _Soft is the window surface itself this frame

static int _FrameWidth = 0;
static int _FrameHeight = 0;
static std::vector<std::uint32_t> _Frame; // the last uploaded game frame, converted
static unsigned _FrameVersion = 0;			// counts its changes, so a scaling of it can be kept
static BackendScaleMode _ScaleMode = BACKEND_SCALE_NEAREST;

// For OPENTS_FPS: nanoseconds spent in each part of presenting, since the last report.
static Uint64 _TimeFrame = 0;		// converting and scaling the game frame
static Uint64 _TimeOverlay = 0;		// drawing the UI over it, between Present and End_Frame
static Uint64 _TimeBlit = 0;		// copying the screen to the window surface
static Uint64 _TimeUpdate = 0;		// handing the window surface to the system
static Uint64 _TimePointer = 0;		// drawing the pointer (RISC OS)
static Uint64 _OverlayStart = 0;

// RISC OS on the Pi: frames shown through the display's hardware overlay, which stretches them
// itself (riscosoverlay.h). Only frames the UI draws nothing over go that way, as the UI is drawn
// at the screen's size; the frame after one it drew over is drawn the usual way.
static bool _ThroughOverlay = false;	// this frame goes to the overlay
static bool _Skipped = false;			// so it wasn't stretched into the screen
static bool _UIDrew = false;			// the UI drew this frame
static bool _UIDrewLast = false;		// ... and the frame before
static int _DestX = 0, _DestY = 0, _DestWidth = 0, _DestHeight = 0;
// The game's 565 frame, and whether _Frame is older than it: through the overlay it is converted
// straight into the overlay's buffer, and into _Frame only for the usual way.
static void const * _Pixels565 = nullptr;
static int _Pitch565 = 0;
static bool _FrameStale = false;

void Soft_Note_UI_Drawn(void)
{
	_UIDrew = true;
}

static void Convert_Frame_To(std::uint32_t * dest, int destpitch, void const * pixels, int pitch);

// The frame and the pointer, into the overlay's buffer (pitch in bytes).
static void Draw_Overlay_Frame(void * pixels, int pitch)
{
	if (_FrameStale) {
		Convert_Frame_To((std::uint32_t *)pixels, pitch / 4, _Pixels565, _Pitch565);
	} else {
		for (int y = 0; y < _FrameHeight; y++) {
			std::memcpy((char *)pixels + (size_t)y * (size_t)pitch, &_Frame[(size_t)y * (size_t)_FrameWidth], (size_t)_FrameWidth * 4);
		}
	}
	RISCOS_Pointer_Draw_Into_Frame((std::uint32_t *)pixels, pitch / 4, _FrameWidth, _FrameHeight, _Soft.RedShift, _Soft.BlueShift,
		_DestX, _DestY, _DestWidth, _DestHeight);
}

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
	_FrameVersion++;
	_Pixels565 = nullptr;
	_FrameStale = false;
	return(true);
}


void Backend_On_Resize(int drawablewidth, int drawableheight)
{
	if (drawablewidth > 0 && drawableheight > 0) {
		Make_Screen(drawablewidth, drawableheight);
	}
}


// The 565 game frame to 32 bits (destpitch in pixels).
static void Convert_Frame_To(std::uint32_t * dest, int destpitch, void const * pixels, int pitch)
{
	for (int y = 0; y < _FrameHeight; y++) {
		std::uint16_t const * source = (std::uint16_t const *)((char const *)pixels + (size_t)y * (size_t)pitch);
		std::uint32_t * row = dest + (size_t)y * (size_t)destpitch;
		for (int x = 0; x < _FrameWidth; x++) {
			row[x] = _Expand565[source[x]];
		}
	}
}


static void Convert_Frame(void const * pixels, int pitch)
{
	Convert_Frame_To(_Frame.data(), _FrameWidth, pixels, pitch);
	_FrameVersion++;
}


// Where each destination column or row samples the frame for smooth scaling: the frame pixel
// to its left (or above) and the weight, out of 256, of the one after it.
struct SmoothTap
{
	int Index;
	unsigned Weight;
};

static void Smooth_Taps(std::vector<SmoothTap> & taps, int sourcesize, int destsize)
{
	taps.resize((size_t)destsize);
	for (int d = 0; d < destsize; d++) {
		// The middle of the destination pixel, in frame pixels (16.16).
		std::int64_t const centre = (((std::int64_t)d * 2 + 1) * sourcesize * 65536) / ((std::int64_t)destsize * 2) - 32768;
		int index = (int)(centre >> 16);
		unsigned weight = (unsigned)((centre >> 8) & 0xFF);
		if (centre < 0) {
			index = 0;
			weight = 0;
		}
		if (index >= sourcesize - 1) {
			index = sourcesize - 1;
			weight = 0;
		}
		taps[(size_t)d] = {index, weight};
	}
}

// Blends two 0xFFxxxxxx pixels, weight out of 256 for the second, a channel pair at a time.
static inline std::uint32_t Blend(std::uint32_t first, std::uint32_t second, unsigned weight)
{
	std::uint32_t const outer = (((first & 0x00FF00FFu) * (256 - weight) + (second & 0x00FF00FFu) * weight) >> 8) & 0x00FF00FFu;
	std::uint32_t const middle = (((first & 0x0000FF00u) * (256 - weight) + (second & 0x0000FF00u) * weight) >> 8) & 0x0000FF00u;
	return(0xFF000000u | outer | middle);
}

// Bilinear scaling, as the Pi's display scales frames through its overlay, so that a frame drawn
// the usual way (with a menu over it) looks like the ones around it. Each frame row is
// stretched across once and kept while the destination rows between it and the next use it.
static void Scale_Frame_Smooth(int destx, int desty, int destwidth, int destheight, int x0, int y0, int x1, int y1)
{
	// The last scaling, kept for as long as the frame and the rectangle stay the same (a menu
	// over a paused game redraws the screen with the same frame under it every time).
	static std::vector<std::uint32_t> kept;
	static unsigned keptversion = 0;
	static int keptrect[6] = {};
	int const rect[6] = {x0, y0, x1, y1, destwidth, destheight};
	size_t const keptwidth = (size_t)(x1 - x0);
	bool const same = !kept.empty() && keptversion == _FrameVersion && std::equal(rect, rect + 6, keptrect);
	if (same) {
		for (int y = y0; y < y1; y++) {
			std::memcpy(_Soft.Pixels + (size_t)y * (size_t)_Soft.Pitch + x0, &kept[(size_t)(y - y0) * keptwidth], keptwidth * 4);
		}
		return;
	}

	static std::vector<SmoothTap> columns, rows;
	static std::vector<std::uint32_t> upper, lower;
	Smooth_Taps(columns, _FrameWidth, destwidth);
	Smooth_Taps(rows, _FrameHeight, destheight);
	upper.resize((size_t)destwidth);
	lower.resize((size_t)destwidth);
	int upperrow = -1, lowerrow = -1;

	auto stretch = [&](int row, std::vector<std::uint32_t> & line) {
		std::uint32_t const * source = &_Frame[(size_t)row * (size_t)_FrameWidth];
		int const last = _FrameWidth - 1;
		for (int x = x0 - destx; x < x1 - destx; x++) {
			SmoothTap const tap = columns[(size_t)x];
			line[(size_t)x] = tap.Weight == 0 ? source[tap.Index] : Blend(source[tap.Index], source[std::min(tap.Index + 1, last)], tap.Weight);
		}
	};

	for (int y = y0; y < y1; y++) {
		SmoothTap const tap = rows[(size_t)(y - desty)];
		int const next = std::min(tap.Index + 1, _FrameHeight - 1);
		if (upperrow != tap.Index) {
			if (lowerrow == tap.Index) {
				std::swap(upper, lower);
				upperrow = lowerrow;
				lowerrow = -1;
			} else {
				stretch(tap.Index, upper);
				upperrow = tap.Index;
			}
		}
		std::uint32_t * dest = _Soft.Pixels + (size_t)y * (size_t)_Soft.Pitch;
		if (tap.Weight == 0) {
			std::memcpy(dest + x0, upper.data() + (x0 - destx), (size_t)(x1 - x0) * 4);
			continue;
		}
		if (lowerrow != next) {
			stretch(next, lower);
			lowerrow = next;
		}
		for (int x = x0; x < x1; x++) {
			dest[x] = Blend(upper[(size_t)(x - destx)], lower[(size_t)(x - destx)], tap.Weight);
		}
	}

	kept.resize(keptwidth * (size_t)(y1 - y0));
	for (int y = y0; y < y1; y++) {
		std::memcpy(&kept[(size_t)(y - y0) * keptwidth], _Soft.Pixels + (size_t)y * (size_t)_Soft.Pitch + x0, keptwidth * 4);
	}
	keptversion = _FrameVersion;
	std::copy(rect, rect + 6, keptrect);
}


// Scaling into the destination rectangle, which is clipped to the screen: nearest-neighbour, or
// smooth where frames otherwise go through the Pi's overlay.
static void Scale_Frame(int destx, int desty, int destwidth, int destheight)
{
	if (destwidth <= 0 || destheight <= 0 || _FrameWidth <= 0 || _FrameHeight <= 0) {
		return;
	}

	int const x0 = destx < 0 ? 0 : destx;
	int const y0 = desty < 0 ? 0 : desty;
	int const x1 = (destx + destwidth > _Soft.Width) ? _Soft.Width : destx + destwidth;
	int const y1 = (desty + destheight > _Soft.Height) ? _Soft.Height : desty + destheight;

	if ((destwidth != _FrameWidth || destheight != _FrameHeight) && RISCOS_Overlay_Available()) {
		Scale_Frame_Smooth(destx, desty, destwidth, destheight, x0, y0, x1, y1);
		return;
	}

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
	// Nothing goes on the screen while the desktop has it (waiting on the icon bar, or quitting).
	if (RISCOS_Desktop_Has_Screen()) {
		static bool told = false;
		if (!told) {
			told = true;
			DebugString("Video: a frame wasn't presented, as the desktop has the screen\n");
		}
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
		_Pixels565 = pixels; // the game's visible surface, which lasts until the frame size changes
		_Pitch565 = pitch;
		_FrameStale = true;
	}

	// The overlay is drawn over the frame each time, so the whole screen is redrawn: the
	// frame covers its rectangle, and only the borders around it need clearing.
	_DestX = destx;
	_DestY = desty;
	_DestWidth = destwidth;
	_DestHeight = destheight;
	_ThroughOverlay = !_UIDrewLast && _FrameWidth > 0 && RISCOS_Overlay_Available();
	// While the overlay is showing, the screen underneath isn't seen. Otherwise this frame is
	// drawn the usual way as well, to put the borders round the overlay.
	_Skipped = _ThroughOverlay && RISCOS_Overlay_Showing();
	// The overlay takes the game's frame as it is if it can; otherwise it's converted first.
	if (_FrameStale && !_Skipped) {
		Convert_Frame(_Pixels565, _Pitch565);
		_FrameStale = false;
	}
	if (!_Skipped) {
		Clear_Borders(destx, desty, destwidth, destheight);
		Scale_Frame(destx, desty, destwidth, destheight);
	}
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
	if (_Window == nullptr || _Screen == nullptr || RISCOS_Desktop_Has_Screen()) {
		return;
	}
	if (_OverlayStart != 0) {
		_TimeOverlay += SDL_GetTicksNS() - _OverlayStart;
		_OverlayStart = 0;
	}
	Uint64 const pointerstart = SDL_GetTicksNS();
	RISCOS_Pointer_Draw(_Soft.Pixels, _Soft.Pitch, _Soft.Width, _Soft.Height, _Soft.RedShift, _Soft.BlueShift);
	_TimePointer += SDL_GetTicksNS() - pointerstart;

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
			DebugString("Video: %.1f frames a second presented, %.1f game frames; ms a frame: scale %.2f, UI %.2f, pointer %.3f, blit %.2f, system %.2f; game frame %d\n",
				count / seconds, (Frame - startframe) / seconds, _TimeFrame / per, _TimeOverlay / per, _TimePointer / per, _TimeBlit / per, _TimeUpdate / per, Frame);
			_TimeFrame = _TimeOverlay = _TimePointer = _TimeBlit = _TimeUpdate = 0;
			start = now;
			count = 0;
			startframe = Frame;
		}
	}

	// OPENTS_QUITFRAME=<n> quits once game frame n has been reached, for timing runs that
	// nobody watches (a game holding the machine can't be told to stop from outside).
	static int const quitframe = getenv("OPENTS_QUITFRAME") != nullptr ? atoi(getenv("OPENTS_QUITFRAME")) : 0;
	if (quitframe > 0 && Frame >= quitframe) {
		DebugString("Video: game frame %d reached, quitting (OPENTS_QUITFRAME)\n", Frame);
		Emergency_Exit();
		exit(0);
	}

	bool const uidrew = _UIDrew;
	_UIDrewLast = uidrew;
	_UIDrew = false;
	if (_ThroughOverlay) {
		_ThroughOverlay = false;
		Uint64 const showstart = SDL_GetTicksNS();
		if (uidrew) {
			// The UI drew over a screen that may not have the frame in it: this frame isn't
			// shown, and the next is drawn the usual way.
			if (_Skipped) {
				return;
			}
		} else if (RISCOS_Overlay_Show(_FrameWidth, _FrameHeight, _Soft.RedShift == 16 ? OVERLAY_XRGB : OVERLAY_XBGR,
				_DestX, _DestY, _DestWidth, _DestHeight, Draw_Overlay_Frame)) {
			_TimeBlit += SDL_GetTicksNS() - showstart;
			if (_Skipped) {
				return;
			}
		} else if (_Skipped) {
			// The overlay failed with nothing drawn on the screen: draw the frame now.
			if (_FrameStale) {
				Convert_Frame(_Pixels565, _Pitch565);
				_FrameStale = false;
			}
			Clear_Borders(_DestX, _DestY, _DestWidth, _DestHeight);
			Scale_Frame(_DestX, _DestY, _DestWidth, _DestHeight);
			RISCOS_Pointer_Draw(_Soft.Pixels, _Soft.Pitch, _Soft.Width, _Soft.Height, _Soft.RedShift, _Soft.BlueShift);
		}
	}
	if (uidrew || !RISCOS_Overlay_Available()) {
		RISCOS_Overlay_Hide();
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
