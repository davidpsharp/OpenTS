/*
** Reads the mouse wheel where SDL does not (RISC OS) and passes it to SDL as wheel events.
** Call it before polling SDL's events. Elsewhere it does nothing.
*/
#pragma once

#ifdef __riscos__
void RISCOS_Poll_Wheel(void);
#else
inline void RISCOS_Poll_Wheel(void) {}
#endif
