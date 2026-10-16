/*
** Game frames shown through the Raspberry Pi's hardware overlay (RISC OS's VideoOverlay module):
** the display stretches the frame to the screen, so neither the scaling nor the copy to the
** screen costs the CPU anything. Elsewhere, or without the module, nothing is shown this way.
*/
#pragma once

#include <cstdint>

// The frames' pixels: 32-bit, with red at the top or the bottom. (VideoOverlay 0.02 on a Pi 4
// refuses 16-bit ones.)
enum OverlayFormat { OVERLAY_XRGB = 2, OVERLAY_XBGR = 3 };

#ifdef __riscos__
// Whether frames can go through an overlay at all (the module is loaded and it isn't turned off).
bool RISCOS_Overlay_Available(void);
// Whether the overlay is on the screen now.
bool RISCOS_Overlay_Showing(void);
// Shows a frame, stretched over the screen rectangle given in pixels: draw puts it into the
// overlay's buffer (pitch in bytes). False if it couldn't be, and then the frame must be drawn
// the usual way; after a failure the overlay isn't tried again until the sizes change.
bool RISCOS_Overlay_Show(int width, int height, OverlayFormat format,
	int destx, int desty, int destwidth, int destheight, void (*draw)(void * pixels, int pitch));
// Takes the overlay off the screen (frames are being drawn the usual way again).
void RISCOS_Overlay_Hide(void);
// Takes the overlay off the screen and closes its window, for the desktop.
void RISCOS_Overlay_Leave(void);
#else
inline bool RISCOS_Overlay_Available(void) { return(false); }
inline bool RISCOS_Overlay_Showing(void) { return(false); }
inline bool RISCOS_Overlay_Show(int, int, OverlayFormat, int, int, int, int, void (*)(void *, int)) { return(false); }
inline void RISCOS_Overlay_Hide(void) {}
inline void RISCOS_Overlay_Leave(void) {}
#endif
