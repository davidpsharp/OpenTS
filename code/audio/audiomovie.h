/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The VQA player's audio handler on top of the audio engine. The player
// pushes its sound track a block at a time and slaves the picture to the
// clock this handler answers. The names are the ones the player calls.

#pragma once

#include "vqaplay.h"


struct AhandleInitParams
{
	unsigned short SampleRate;
	unsigned char Channels;
	unsigned char BitsPerSample;
	uint32_t Flags;
	void * Callback1;
	void * Callback2;
};

typedef int32_t (__cdecl * AHANDLE_CALLBACK_1)(VQAHandle * vqa);
typedef int32_t (__cdecl * AHANDLE_CALLBACK_2)(VQAHandle * vqa, void * buffer);

uint32_t __cdecl Simple_Timer_Callback_Audio_Handler(VQAHandle * vqa);
uint32_t __cdecl Timer_Callback_Audio_Handler(VQAHandle * vqa);

int32_t __cdecl Lock_Audio_Handler(void);
int32_t __cdecl Unlock_Audio_Handler(void);
intptr_t __cdecl Stream_Audio_Handler(VQAHandle * vqa, int32_t action, void * buffer, int32_t nbytes);
