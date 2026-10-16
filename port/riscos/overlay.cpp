/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** Game frames through the Raspberry Pi's hardware overlay. Drawn the usual way, each frame is
** stretched to the screen by the CPU and then SDL copies the whole screen picture into screen
** memory, about 8 ms of every frame on a Pi 4. Through an overlay the frame is converted once, at
** its own size, straight into a buffer the display shows, stretched by the display itself.
**
** The VideoOverlay module (in !System on RISC OS for the Pi, loaded by !Run) shows an overlay
** in a Wimp window, so the game opens a window over the whole screen for it, with nothing in it
** to redraw. A "basic" overlay covers everything, menus too, which is what a full screen game
** wants. The display switches to a newly shown buffer at the next vsync, so a buffer can be
** written only when it is neither on the screen nor waiting for that vsync. With three buffers
** one is always free, so a frame never waits; with two (the GPU short of memory) a frame waits
** for the vsync when the other buffer is still waiting for it.
**
** How the module behaves on a Pi 4 (VideoOverlay 0.02) is from riscos-mesa's
** egl/parts/overlay.c, by adyoull (MIT licence): VideoOverlay_Vet refuses every format, so
** Create is the test; the old overlay isn't freed by a mode change; buffers are mapped only
** while being written.
**
** *Set OpenTS$NoOverlay 1 turns it off, to compare.
*/

#include "riscosoverlay.h"

#include <SDL3/SDL.h>

#include <kernel.h>
#include <swis.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

enum { OV_CREATE, OV_DESTROY, OV_DISPLAY, OV_MAP, OV_UNMAP, OV_SET_SCALE, OV_SET_WINDOW, OV_SET_POSITION, OV_COUNT };

struct
{
	char const * Name;
	int Number;
} Swis[OV_COUNT] = {
	{"VideoOverlay_Create", 0}, {"VideoOverlay_Destroy", 0}, {"VideoOverlay_DisplayBuffer", 0},
	{"VideoOverlay_MapBuffer", 0}, {"VideoOverlay_UnmapBuffer", 0}, {"VideoOverlay_SetScale", 0},
	{"VideoOverlay_SetWindow", 0}, {"VideoOverlay_SetPosition", 0},
};

int Module = 0;			// 1 VideoOverlay is there, -1 not, 0 not looked yet
int Overlay = 0;		// the overlay's ID, 0 for none
int Window = -1;		// the window it's shown in
bool WindowOpen = false;
bool Shown = false;
bool Failed = false;	// don't try again until the sizes change
int Banks = 0;
int OnScreen = -1;		// the buffer the display shows
int Pending = -1;		// the buffer it switches to at the next vsync
int PendingVsync = 0;	// OS_Byte 176's count when that one was asked for
int FrameWidth = 0, FrameHeight = 0, FrameFormat = 0;
int ScreenWidth = 0, ScreenHeight = 0;
int Placed[4] = {-1, -1, -1, -1};

bool Call(int which, int r0, int r1)
{
	_kernel_swi_regs regs;
	regs.r[0] = r0;
	regs.r[1] = r1;
	return(_kernel_swi(Swis[which].Number, &regs, &regs) == nullptr);
}

void Report(char const * what, _kernel_oserror const * error)
{
	fprintf(stderr, "overlay: %s%s%s\n", what, error != nullptr ? ": " : "", error != nullptr ? error->errmess : "");
}

int Mode_Variable(int variable)
{
	_kernel_swi_regs regs;
	regs.r[0] = -1;
	regs.r[1] = variable;
	_kernel_swi(OS_ReadModeVariable, &regs, &regs);
	return(regs.r[2]);
}

// OS_Byte 176's counter goes down by one at every vsync.
int Vsync_Count(void)
{
	return(_kernel_osbyte(176, 0, 255) & 0xff); // R1, the count before (unchanged by AND 255)
}

bool Module_Loaded(void)
{
	if (Module != 0) {
		return(Module > 0);
	}
	Module = -1;
	char const * off = getenv("OpenTS$NoOverlay");
	if (off != nullptr && off[0] != '\0') {
		Report("off (OpenTS$NoOverlay is set)", nullptr);
		return(false);
	}
	for (auto & swi : Swis) {
		_kernel_swi_regs regs;
		regs.r[1] = (int)swi.Name;
		if (_kernel_swi(OS_SWINumberFromString, &regs, &regs) != nullptr) {
			Report("VideoOverlay isn't loaded, so frames are drawn the usual way", nullptr);
			return(false);
		}
		swi.Number = regs.r[0] | 0x20000; // the X form
	}
	Module = 1;
	return(true);
}

void Destroy(void)
{
	if (Overlay != 0) {
		if (Shown) {
			Call(OV_DISPLAY, Overlay, -1);
		}
		Call(OV_DESTROY, Overlay, 0);
	}
	Overlay = 0;
	Shown = false;
	Placed[0] = -1;
}

// A window over the whole screen, which the Wimp needn't ask us to redraw (nothing shows in it).
bool Open_Window(void)
{
	int const right = ScreenWidth << Mode_Variable(4);
	int const top = ScreenHeight << Mode_Variable(5);
	_kernel_swi_regs regs;
	if (Window == -1) {
		int block[23] = {};
		block[0] = 0;
		block[1] = 0;
		block[2] = right;
		block[3] = top;
		block[4] = 0;					// scroll x, y
		block[5] = 0;
		block[6] = -1;					// on top
		block[7] = (int)0x80000010u;	// new format; the Wimp redraws it (there's nothing to draw)
		block[8] = 0xffffffff;			// no title or work area colours: nothing is filled
		block[9] = 0x000c0103;
		block[10] = 0;					// work area: 0,-top to right,0
		block[11] = -top;
		block[12] = right;
		block[13] = 0;
		block[14] = 0;					// no title bar
		block[15] = 0;					// work area button type: never
		block[16] = 1;					// the Wimp sprite area
		block[17] = 0;					// minimum size
		block[18] = 0;					// title data
		block[21] = 0;					// no icons
		regs.r[1] = (int)block;
		_kernel_oserror const * error = _kernel_swi(Wimp_CreateWindow, &regs, &regs);
		if (error != nullptr) {
			Report("the window couldn't be made", error);
			return(false);
		}
		Window = regs.r[0];
	}
	int block[8] = {Window, 0, 0, right, top, 0, 0, -1};
	regs.r[1] = (int)block;
	_kernel_oserror const * error = _kernel_swi(Wimp_OpenWindow, &regs, &regs);
	if (error != nullptr) {
		Report("the window couldn't be opened", error);
		return(false);
	}
	WindowOpen = true;
	return(true);
}

bool Create(void)
{
	_kernel_swi_regs regs;
	regs.r[0] = 5; // the current task
	if (_kernel_swi(Wimp_ReadSysInfo, &regs, &regs) != nullptr || regs.r[0] == 0) {
		return(false);
	}
	int const task = regs.r[0];

	_kernel_oserror const * error = nullptr;
	for (Banks = 3; Banks >= 2; Banks--) {
		// A mode selector: 32bpp in the frame's colour order (bit 14 of the mode flags puts red
		// at the top), 16M colours, and the number of buffers.
		int selector[12] = {1, FrameWidth, FrameHeight, 5, -1, 0, FrameFormat == OVERLAY_XRGB ? 0x4000 : 0, 3, -1, 13, Banks, -1};
		regs.r[0] = (int)selector;
		regs.r[1] = (1 << 16) | 1; // square pixels
		regs.r[2] = 0;
		regs.r[3] = task;
		error = _kernel_swi(Swis[OV_CREATE].Number, &regs, &regs);
		if (error != nullptr) {
			break;
		}
		Overlay = regs.r[0];
		// Map each buffer once now, so a GPU without room for them shows up here.
		int bank = 0;
		for (; bank < Banks; bank++) {
			if (!Call(OV_MAP, Overlay, bank)) {
				break;
			}
			Call(OV_UNMAP, Overlay, bank);
		}
		if (bank == Banks) {
			break;
		}
		Call(OV_DESTROY, Overlay, 0);
		Overlay = 0;
	}
	if (Overlay == 0) {
		Report("none could be made for the frame's size", error);
		return(false);
	}
	if (!Call(OV_SET_WINDOW, Overlay, Window)) {
		Report("it couldn't be put in its window", nullptr);
		Destroy();
		return(false);
	}
	OnScreen = -1;
	Pending = -1;
	fprintf(stderr, "overlay: %dx%d frames, %d buffers\n", FrameWidth, FrameHeight, Banks);
	return(true);
}

// Stretches the overlay over the rectangle (pixels from the top left), in the window's
// work area coordinates (OS units, y up from the top).
bool Place(int x, int y, int width, int height)
{
	if (Placed[0] == x && Placed[1] == y && Placed[2] == width && Placed[3] == height) {
		return(true);
	}
	int const xeig = Mode_Variable(4);
	int const yeig = Mode_Variable(5);
	_kernel_swi_regs regs;
	regs.r[0] = Overlay;
	regs.r[1] = width;
	regs.r[2] = height;
	regs.r[3] = (1 << 16) | 1;
	if (_kernel_swi(Swis[OV_SET_SCALE].Number, &regs, &regs) != nullptr) {
		return(false);
	}
	regs.r[0] = Overlay;
	regs.r[1] = x << xeig;
	regs.r[2] = -(y << yeig);
	regs.r[3] = x << xeig;
	regs.r[4] = -((y + height) << yeig);
	regs.r[5] = (x + width) << xeig;
	regs.r[6] = -(y << yeig);
	if (_kernel_swi(Swis[OV_SET_POSITION].Number, &regs, &regs) != nullptr) {
		return(false);
	}
	Placed[0] = x;
	Placed[1] = y;
	Placed[2] = width;
	Placed[3] = height;
	return(true);
}

bool Draw_Frame(int buffer, void (*draw)(void * pixels, int pitch))
{
	_kernel_swi_regs regs;
	regs.r[0] = Overlay;
	regs.r[1] = buffer;
	if (_kernel_swi(Swis[OV_MAP].Number, &regs, &regs) != nullptr) {
		return(false);
	}
	int const * planes = (int const *)regs.r[0]; // plane 0: its address and row length in bytes
	draw((void *)planes[0], planes[1]);
	Call(OV_UNMAP, Overlay, buffer);
	return(true);
}

void Fail(char const * what)
{
	Report(what, nullptr);
	Destroy();
	Failed = true;
}

} // namespace

bool RISCOS_Overlay_Available(void)
{
	return(Module_Loaded() && !Failed);
}

bool RISCOS_Overlay_Showing(void)
{
	return(Shown);
}

bool RISCOS_Overlay_Show(int width, int height, OverlayFormat format,
	int destx, int desty, int destwidth, int destheight, void (*draw)(void * pixels, int pitch))
{
	if (!Module_Loaded() || draw == nullptr || width <= 0 || height <= 0) {
		return(false);
	}

	int const screenwidth = Mode_Variable(11) + 1;
	int const screenheight = Mode_Variable(12) + 1;
	bool const resized = width != FrameWidth || height != FrameHeight || format != FrameFormat
		|| screenwidth != ScreenWidth || screenheight != ScreenHeight;
	if (resized) {
		Destroy();
		Failed = false;
		FrameWidth = width;
		FrameHeight = height;
		FrameFormat = format;
		ScreenWidth = screenwidth;
		ScreenHeight = screenheight;
		if (Window != -1) {
			// The window is the screen's size.
			_kernel_swi_regs regs;
			int block[1] = {Window};
			regs.r[1] = (int)block;
			_kernel_swi(Wimp_DeleteWindow, &regs, &regs);
			Window = -1;
			WindowOpen = false;
		}
	}
	if (Failed) {
		return(false);
	}
	if (!WindowOpen && !Open_Window()) {
		Failed = true;
		return(false);
	}
	if (Overlay == 0 && !Create()) {
		Failed = true;
		return(false);
	}
	if (!Place(destx, desty, destwidth, destheight)) {
		Fail("it couldn't be stretched over the screen");
		return(false);
	}

	static Uint64 waited = 0, copied = 0, shown = 0;
	static unsigned frames = 0, waits = 0;
	Uint64 const start = SDL_GetTicksNS();
	if (Pending >= 0 && Vsync_Count() != PendingVsync) {
		OnScreen = Pending; // a vsync has passed, so the display has switched to it
		Pending = -1;
	}
	int buffer = 0;
	while (buffer == OnScreen || buffer == Pending) {
		buffer++;
	}
	if (buffer >= Banks) {
		_kernel_osbyte(19, 0, 0); // two buffers, both busy: wait for the switch
		waits++;
		OnScreen = Pending;
		Pending = -1;
		buffer = OnScreen == 0 ? 1 : 0;
	}
	Uint64 const copystart = SDL_GetTicksNS();
	if (!Draw_Frame(buffer, draw)) {
		Fail("a frame couldn't be drawn in");
		return(false);
	}
	Uint64 const showstart = SDL_GetTicksNS();
	PendingVsync = Vsync_Count();
	if (!Call(OV_DISPLAY, Overlay, buffer)) {
		Fail("a frame couldn't be shown");
		return(false);
	}
	Uint64 const done = SDL_GetTicksNS();
	waited += copystart - start;
	copied += showstart - copystart;
	shown += done - showstart;
	// With OPENTS_FPS, where the time goes, every 300 frames.
	static bool const report = getenv("OPENTS_FPS") != nullptr;
	if (report && ++frames == 300) {
		fprintf(stderr, "overlay: ms a frame: vsync wait %.2f (%u of %u frames), drawing in %.2f, show %.2f\n",
			waited / 3.0e8, waits, frames, copied / 3.0e8, shown / 3.0e8);
		waited = copied = shown = 0;
		frames = waits = 0;
	}
	Pending = buffer;
	Shown = true;
	return(true);
}

void RISCOS_Overlay_Hide(void)
{
	if (Shown) {
		Call(OV_DISPLAY, Overlay, -1);
		Shown = false;
	}
}

void RISCOS_Overlay_Leave(void)
{
	RISCOS_Overlay_Hide();
	if (WindowOpen) {
		_kernel_swi_regs regs;
		int block[1] = {Window};
		regs.r[1] = (int)block;
		_kernel_swi(Wimp_CloseWindow, &regs, &regs);
		WindowOpen = false;
	}
}
