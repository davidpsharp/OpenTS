/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** Typed text on RISC OS. SDL's RISC OS driver scans the keyboard for keys going up and down
** but sends no text events, so nothing could be typed into the game's text boxes; and as
** nothing read the keyboard buffer, typing filled it until RISC OS beeped. The buffer is read
** here (INKEY(0)) and each printable character (RISC OS's Latin-1) is pushed to SDL as text,
** as other platforms send it. Control characters are left to the key events.
*/

#include "riscostext.h"

#include <SDL3/SDL.h>

#include <kernel.h>

// The pushed events point into this ring, which lasts several frames, long enough for the
// game to have read them.
static char _Text[64][4];
static unsigned _Next = 0;

static int Read_Key(void)
{
	int const result = _kernel_osbyte(129, 0, 0); // INKEY(0): r1 the character, r2 0 if there was one
	if (((result >> 8) & 0xff) != 0) {
		return(-1);
	}
	return(result & 0xff);
}

void RISCOS_Poll_Text(void)
{
	for (int count = 0; count < 32; count++) {
		int const key = Read_Key();
		if (key < 0) {
			return;
		}
		if (key == 0) {
			Read_Key(); // a function key's code follows a zero
			continue;
		}
		if (key < 32 || (key >= 0x7f && key < 0xa0)) {
			continue;
		}

		char * text = _Text[_Next++ % 64];
		if (key < 0x80) {
			text[0] = (char)key;
			text[1] = '\0';
		} else {
			text[0] = (char)(0xc0 | (key >> 6));
			text[1] = (char)(0x80 | (key & 0x3f));
			text[2] = '\0';
		}

		SDL_Event event;
		SDL_zero(event);
		event.type = SDL_EVENT_TEXT_INPUT;
		event.text.timestamp = SDL_GetTicksNS();
		event.text.windowID = SDL_GetWindowID(SDL_GetKeyboardFocus());
		event.text.text = text;
		SDL_PushEvent(&event);
	}
}
