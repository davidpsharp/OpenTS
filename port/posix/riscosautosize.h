/*
** The game's size on RISC OS when the player hasn't chosen one: the desktop's shape, as big as
** this machine can draw it smoothly, from a short speed test run the first time (riscos/autosize.cpp).
** Elsewhere this does nothing.
*/
#pragma once

class CCINIClass;

#ifdef __riscos__
// Called with the settings just read. If width and height are the player's own choice (set in
// Options > Display) they're kept. Otherwise they become the size picked for this machine and
// desktop, and the speed test's results and the pick are kept in the [RISCOS] section of the
// settings, saved to settingsfile. OpenTS$Retune set runs the test again.
void RISCOS_Auto_Size(CCINIClass & ini, char const * settingsfile, int & width, int & height);
#else
inline void RISCOS_Auto_Size(CCINIClass &, char const *, int &, int &) {}
#endif
