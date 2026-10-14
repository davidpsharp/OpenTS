/*
** Reads typed characters where SDL does not (RISC OS) and passes them to SDL as text events.
** Call it before polling SDL's events. Elsewhere it does nothing.
*/
#pragma once

#ifdef __riscos__
void RISCOS_Poll_Text(void);
#else
inline void RISCOS_Poll_Text(void) {}
#endif
