/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "ownrdraw.h"

#include "_surface.h"
#include "_xmouse.h"
#include "arraylist.h"
#include "dsurface.h"
#include "globals.h"
#include "keyboard.h"
#include "misc.h"
#include "sdl/sdlwindow.h"
#include "surface.h"

#include <cmath>
#include <cstring>


using namespace OwnerDraw;


static int _mouse_counter;

unsigned short ODRComponentMask;
unsigned short ODGComponentMask;
unsigned short ODBComponentMask;


/// <summary>
/// Builds the color component masks used for blending.
/// The masks depend on how the display surface packs its pixels, so this cannot run
/// before the video mode is set.
/// </summary>
void ODInitMasks(void)
{
	ODRComponentMask = 255;
	ODRComponentMask = ODRComponentMask >> DSurface::Get_Red_Left();
	ODRComponentMask <<= DSurface::Get_Red_Right();

	ODGComponentMask = 255;
	ODGComponentMask = ODGComponentMask >> DSurface::Get_Green_Left();
	ODGComponentMask <<= DSurface::Get_Green_Right();

	ODBComponentMask = 255;
	ODBComponentMask = ODBComponentMask >> DSurface::Get_Blue_Left();
	ODBComponentMask <<= DSurface::Get_Blue_Right();
}


/// <summary>
/// Builds the blend masks once; call it after the video mode is set.
/// </summary>
void OwnerDraw::Prepare_Resources(void)
{
	static bool _inited = false;
	if (!_inited) {
		ODInitMasks();
		_inited = true;
	}
}


/// <summary>
/// Names a hotkey for the player, as its modifiers and then its key, for example
/// "Ctrl+Shift+A".
/// </summary>
std::string Build_Hotkey_String(KeyNumType key)
{
	unsigned const code = (unsigned)key;
	std::string name;

	if ((code & WWKEY_ALT_BIT) != 0) {
		name += Main_Window_Key_Name(VK_MENU) + "+";
	}
	if ((code & WWKEY_CTRL_BIT) != 0) {
		name += Main_Window_Key_Name(VK_CONTROL) + "+";
	}
	if ((code & WWKEY_SHIFT_BIT) != 0) {
		name += Main_Window_Key_Name(VK_SHIFT) + "+";
	}

	name += Main_Window_Key_Name(code & 0xFF);
	return(name);
}


struct EzFont {
	char FaceName[128];
	int DeciPtWidth;
	int DeciPtHeight;
	int Attributes;
	HFONT FontHandle;
};

ArrayList<EzFont> g_EzFonts;


#define EZ_ATTR_BOLD		  1
#define EZ_ATTR_ITALIC		  2
#define EZ_ATTR_UNDERLINE	  4
#define EZ_ATTR_STRIKEOUT	  8
HFONT Ez_Create_Font (HDC hdc, const char * face_name, int decipt_width, int decipt_height, int attributes);


/// <summary>
/// Fetches a font of the typeface and point size requested.
/// Every font built here is kept, so a repeated request for the same description returns
/// the same handle rather than creating another GDI object.
/// </summary>
/// <param name="hdc">The device context to build the font for. If this is NULL, the
/// font is only looked up and never created.</param>
/// <param name="decipt_width">The character width in tenths of a point.</param>
/// <param name="decipt_height">The character height in tenths of a point.</param>
/// <param name="attributes">Bit flags of the EZ_ATTR_ style attributes to apply.</param>
/// <returns>Returns with a handle to the font, or NULL if it was neither cached nor
/// able to be created.</returns>
/// <remarks>The returned handle stays owned by the font cache. Do not delete it.</remarks>
HFONT WS_Get_Font(HDC hdc, const char * face_name, int decipt_width, int decipt_height, int attributes)
{
	EzFont font;

	for (int index = 0; index < g_EzFonts.length(); index++) {
		g_EzFonts.get(font, index);
		if (!strcmp(font.FaceName, face_name) && font.DeciPtWidth == decipt_width && font.DeciPtHeight == decipt_height && font.Attributes == attributes) {
			return(font.FontHandle);
		}
	}

	if (hdc == NULL) {
		return(NULL);
	}

	HFONT hFont = Ez_Create_Font(hdc, face_name, decipt_width, decipt_height, attributes);

	if (hFont == NULL) {
		return(NULL);
	}

	strcpy(font.FaceName, face_name);
	font.DeciPtWidth = decipt_width;
	font.DeciPtHeight = decipt_height;
	font.Attributes = attributes;
	font.FontHandle = hFont;

	if (g_EzFonts.addTail(font)) {
		return(hFont);
	}

	return(NULL);
}

/// <summary>
/// Creates a font of the typeface and point size requested.
/// This routine maps the requested decipoint dimensions through the device context's
/// current transform, so the font it builds matches the coordinate space the caller
/// draws in. Use WS_Get_Font in preference to this routine -- that one caches its fonts.
/// </summary>
/// <param name="hdc">The device context the font is to be built for.</param>
/// <param name="decipt_width">The character width in tenths of a point. Zero lets the
/// typeface choose its aspect.</param>
/// <param name="decipt_height">The character height in tenths of a point.</param>
/// <param name="attributes">Bit flags of the EZ_ATTR_ style attributes to apply.</param>
/// <returns>Returns with a handle to the font created, or NULL if it could not be
/// created.</returns>
/// <remarks>The caller takes ownership of the font handle.</remarks>
HFONT Ez_Create_Font (HDC hdc, const char * face_name, int decipt_width,
					int decipt_height, int attributes)
{
#ifndef _WIN32
	// Fonts come from GDI, which only Windows has, so this text is not drawn elsewhere.
	return(NULL);
#else
	HFONT		hFont ;
	LOGFONT	lf ;
	POINT		pt ;
	TEXTMETRIC tm ;

	SaveDC (hdc) ;

	SetGraphicsMode (hdc, GM_ADVANCED) ;
	ModifyWorldTransform (hdc, NULL, MWT_IDENTITY) ;
	SetViewportOrgEx (hdc, 0, 0, NULL) ;
	SetWindowOrgEx   (hdc, 0, 0, NULL) ;

	pt.x = decipt_width ;
	pt.y = decipt_height ;

	DPtoLP (hdc, &pt, 1) ;

	lf.lfHeight			= -pt.y ;
	lf.lfWidth			= 0 ;
	lf.lfEscapement		= 0 ;
	lf.lfOrientation	= 0 ;
	lf.lfWeight		 = attributes & EZ_ATTR_BOLD	   ? 700 : 0 ;
	lf.lfItalic		 = attributes & EZ_ATTR_ITALIC    ?   1 : 0 ;
	lf.lfUnderline 	 = attributes & EZ_ATTR_UNDERLINE ?   1 : 0 ;
	lf.lfStrikeOut 	 = attributes & EZ_ATTR_STRIKEOUT ?   1 : 0 ;
	lf.lfCharSet		= ANSI_CHARSET ;
	lf.lfOutPrecision	= 0 ;
	lf.lfClipPrecision	= 0 ;
	lf.lfQuality		= 0 ;
	lf.lfPitchAndFamily	= 0 ;

	strcpy (lf.lfFaceName, face_name) ;

	hFont = CreateFontIndirect (&lf) ;

	if (decipt_width != 0) {
		hFont = (HFONT) SelectObject (hdc, hFont) ;
		GetTextMetrics (hdc, &tm) ;
		DeleteObject (SelectObject (hdc, hFont)) ;
		lf.lfWidth = (int) (tm.tmAveCharWidth *
									fabs (pt.x) / fabs (pt.y) + 0.5);
		hFont = CreateFontIndirect (&lf) ;
	}

	RestoreDC (hdc, -1);
	return(hFont);
#endif
}



/// <summary>
/// Draws a line of text into the rectangle on a surface, aligned as asked.
/// A full-screen game that does not hold the focus draws nothing and returns zero.
/// </summary>
/// <param name="len">The number of characters of the text to draw.</param>
/// <param name="surface">The surface to draw upon, or NULL to draw on the alternate
/// surface.</param>
/// <returns>Returns with the pixel width of the text.</returns>
int OD_Draw_Text(COLORREF color, HFONT font, Rect const & rect, const char * text, int len, int x_alignment, int y_alignment, Surface * surface)
{
	if (!GameInFocus && !WindowedMode) {
		return(0);
	}

	DSurface *destsurf = (DSurface *)surface;
	if (!surface) {
		destsurf = (DSurface *)AlternateSurface;
	}

	int lock_count = 0;
	while (destsurf->Is_Locked()) {
		lock_count++;
		destsurf->Unlock();
	}

	SIZE text_size;

	HDC hDC = destsurf->GetDC();
	if (hDC) {

		if (font) {
			SelectObject(hDC, font);
		}

		SetTextColor(hDC, color);
		SetBkMode(hDC, TRANSPARENT);

		GetTextExtentPoint32(hDC, text, len, &text_size);

		int x_offset = rect.X;
		int y_offset = rect.Y;

		if (x_alignment == OD_TEXT_ALIGN_MIN) {
			x_offset += (rect.Width - text_size.cx + 1) / 2;
		} else if (x_alignment == OD_TEXT_ALIGN_CENTER) {
			x_offset += (text_size.cx + 1) / -2;
		} else if (x_alignment == OD_TEXT_ALIGN_MAX) {
			x_offset += -1 - text_size.cx;
		}

		if (y_alignment == OD_TEXT_ALIGN_MIN) {
			y_offset += (rect.Height - text_size.cy + 1) / 2;
		} else if (y_alignment == OD_TEXT_ALIGN_CENTER) {
			y_offset += (text_size.cy + 1) / -2;
		} else if (y_alignment == OD_TEXT_ALIGN_MAX) {
			y_offset += -1 - text_size.cy;
		}

		TextOut(hDC, x_offset, y_offset, text, len);
		destsurf->ReleaseDC(hDC);
	} else {
		text_size.cx = 0;
	}

	while (lock_count) {
		destsurf->Lock();
		lock_count--;
	}

	return(text_size.cx);
}


/// <summary>
/// Takes the mouse away from the game so that a dialog may use it.
/// The game cursor gives up its capture, leaving Windows free to drive the dialog and its
/// controls.
/// </summary>
/// <returns>Returns with the number of captures now outstanding.</returns>
/// <remarks>Each call must be matched by a call to Release_Mouse.</remarks>
int OwnerDraw::Capture_Mouse(void)
{
	if (MouseCursor != NULL) {
		if (MouseCursor->Is_Captured() == true) {
			MouseCursor->Release_Mouse();
		}
	}
	_mouse_counter++;
	return(_mouse_counter);
}


/// <summary>
/// Gives the mouse back to the game.
/// This routine undoes one Capture_Mouse. Only when the last dialog has finished with the
/// mouse does the game cursor take it back.
/// </summary>
/// <returns>Returns with the number of captures still outstanding.</returns>
int OwnerDraw::Release_Mouse(void)
{
	if (_mouse_counter > 0) {
		_mouse_counter--;
	}
	if (_mouse_counter == 0) {
		if (MouseCursor != NULL) {
			if (!MouseCursor->Is_Captured()) {
				MouseCursor->Capture_Mouse();
			}
		}
	}
	return(_mouse_counter);
}
