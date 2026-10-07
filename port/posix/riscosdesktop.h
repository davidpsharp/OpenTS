/*
** The game on the RISC OS desktop: an icon on the icon bar that starts it, and Shift+F12 (on the
** desktop it brings the icon bar to the front) to go back to the desktop with the game paused,
** until the icon is clicked again. The icon's menu has the standard Info and Quit. Elsewhere
** these do nothing.
*/
#pragma once

#ifdef __riscos__
// Starts the game's Wimp task, puts its icon on the icon bar and, unless told not to (wait is
// false, or OPENTS_NODESKTOP is set), waits there for a click before the game takes the screen.
void RISCOS_Desktop_Start(bool wait);

// True for the key that goes back to the desktop: Shift+F12. sdl_key and sdl_mod are SDL3's.
bool RISCOS_Desktop_Is_Leave_Key(unsigned sdl_key, unsigned sdl_mod);

// Gives the screen back to the desktop and waits, paused, until the icon is clicked (or the
// game is quit from its menu). The caller pauses the game around it.
void RISCOS_Desktop_Suspend(void);
#else
inline void RISCOS_Desktop_Start(bool) {}
inline bool RISCOS_Desktop_Is_Leave_Key(unsigned, unsigned) { return(false); }
inline void RISCOS_Desktop_Suspend(void) {}
#endif

// The SDL sound output paused for the desktop and back (audiodevice_sdl.cpp); with no output
// open, nothing.
void Audio_SDL_Pause_For_Desktop(bool pause);
