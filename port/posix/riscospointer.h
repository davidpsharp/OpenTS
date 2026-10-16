/*
** The game's pointer on RISC OS, drawn into each frame. SDL's RISC OS driver has no pointers
** of its own: any SDL cursor shows the desktop's pointer. Elsewhere these do nothing.
*/
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#ifdef __riscos__
// Keeps a copy of a cursor's picture (0xAARRGGBB, rows top first) to draw while it is SDL's.
void RISCOS_Pointer_Add(SDL_Cursor * cursor, unsigned int const * pixels, int width, int height, int hotx, int hoty);
void RISCOS_Pointer_Remove(SDL_Cursor * cursor);
// The pointer drawn when SDL's cursor has no picture, such as SDL's default; NULL for none.
void RISCOS_Pointer_Set_Arrow(SDL_Cursor * cursor);
// Draws SDL's cursor at the mouse over a finished frame, and keeps the desktop's pointer off
// while it does. The screen is 32-bit, red at bit redshift and blue at bit blueshift.
void RISCOS_Pointer_Draw(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift);
// The same into a frame that is shown stretched over the screen rectangle given (a hardware
// overlay's): at the mouse's place in the frame, and at the frame's scale.
void RISCOS_Pointer_Draw_Into_Frame(std::uint32_t * pixels, int pitch, int width, int height, int redshift, int blueshift,
	int destx, int desty, int destwidth, int destheight);
#else
inline void RISCOS_Pointer_Add(SDL_Cursor *, unsigned int const *, int, int, int, int) {}
inline void RISCOS_Pointer_Remove(SDL_Cursor *) {}
inline void RISCOS_Pointer_Set_Arrow(SDL_Cursor *) {}
inline void RISCOS_Pointer_Draw(std::uint32_t *, int, int, int, int, int) {}
inline void RISCOS_Pointer_Draw_Into_Frame(std::uint32_t *, int, int, int, int, int, int, int, int, int) {}
#endif
