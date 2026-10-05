/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The mouse wheel on RISC OS. SDL's RISC OS driver reads only the pointer and buttons, so
** the wheel is read here, from the totals RISC OS 5 keeps (OS_Pointer 2), and each change is
** pushed to SDL as a wheel event for the game to handle as it does elsewhere. A RISC OS that
** does not know the call reports an error once, and the wheel stays off.
*/

#include "riscoswheel.h"
#include "dbgprint.h"

#include <SDL3/SDL.h>

#include <kernel.h>
#include <swis.h>

static bool _Started = false;
static bool _Failed = false;
static int _LastX = 0;
static int _LastY = 0;
static int _Logged = 0;

static bool Read_Wheel(int & x, int & y)
{
	_kernel_swi_regs regs;
	regs.r[0] = 2;
	_kernel_oserror * error = _kernel_swi(OS_Pointer, &regs, &regs);
	if (error != nullptr) {
		DebugString("Wheel: OS_Pointer 2 failed (%s), so the wheel is off\n", error->errmess);
		return(false);
	}
	x = regs.r[0];
	y = regs.r[1];
	return(true);
}

void RISCOS_Poll_Wheel(void)
{
	if (_Failed) {
		return;
	}

	int x, y;
	if (!Read_Wheel(x, y)) {
		_Failed = true;
		return;
	}
	if (!_Started) {
		_Started = true;
		_LastX = x;
		_LastY = y;
		return;
	}

	int const dx = x - _LastX;
	int const dy = y - _LastY;
	if (dx == 0 && dy == 0) {
		return;
	}
	_LastX = x;
	_LastY = y;

	if (_Logged < 10) {
		_Logged++;
		DebugString("Wheel: totals %d,%d, change %d,%d\n", x, y, dx, dy);
	}

	float mousex = 0.0f, mousey = 0.0f;
	SDL_GetMouseState(&mousex, &mousey);

	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.timestamp = SDL_GetTicksNS();
	event.wheel.windowID = SDL_GetWindowID(SDL_GetMouseFocus());
	event.wheel.which = 0;
	event.wheel.x = (float)dx;
	event.wheel.y = (float)dy;
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	event.wheel.mouse_x = mousex;
	event.wheel.mouse_y = mousey;
	event.wheel.integer_x = dx;
	event.wheel.integer_y = dy;
	SDL_PushEvent(&event);
}
