/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The game on the RISC OS desktop (see riscosdesktop.h).
**
** SDL3's RISC OS port draws straight onto the desktop's screen, in its mode, without being a
** Wimp task, so the desktop stops while the game runs. Here the game starts a Wimp task of its
** own with an icon on the icon bar, and polls the Wimp itself while it is not on the screen: at
** the start until the icon is clicked, and after Shift+F12, when the screen, pointer and Escape
** key are handed back to the desktop (which redraws itself) and the game waits, paused.
**
** The icon's menu and Info window follow the standard ones (Edit's, for instance): the menu has
** "Info" leading to "About this program" with Name, Purpose, Author and Version, then "Quit".
*/

#include "riscosdesktop.h"
#include "opents_build.h"
#include "opents_version.h"

#include <kernel.h>
#include <pthread.h>
#include <swis.h>

#include <SDL3/SDL.h>

// The game's (startup.cpp): everything shut down in order, as before exit.
void Emergency_Exit(void);

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

int TaskHandle = 0;
int IconHandle = -1;
int InfoWindow = -1;

char const AppSprite[] = "!opents";
char const AppName[] = "OpenTS";
char const AppPurpose[] = "Tiberian Sun";
char const AppAuthor[] = "Electronic Arts; OpenTS";


bool Swi(int swi, _kernel_swi_regs & regs)
{
	_kernel_oserror * error = _kernel_swi(swi, &regs, &regs);
	if (error != nullptr) {
		fprintf(stderr, "desktop: SWI &%X: %s\n", swi, error->errmess);
		return(false);
	}
	return(true);
}


/*
** Icon blocks, as in a window or Wimp_CreateIcon: the bounding box, flags and 12 bytes of data.
*/
struct Icon
{
	int x0, y0, x1, y1;
	unsigned flags;
	union {
		char text[12];
		struct {
			char const * buffer;
			char const * validation;
			int length;
		} ind;
	} data;
};


void Plain_Text(Icon & icon, int x0, int y0, int x1, int y1, unsigned flags, char const * text)
{
	icon.x0 = x0;
	icon.y0 = y0;
	icon.x1 = x1;
	icon.y1 = y1;
	icon.flags = flags;
	memset(icon.data.text, 0, sizeof(icon.data.text));
	strncpy(icon.data.text, text, sizeof(icon.data.text));
}


void Indirected(Icon & icon, int x0, int y0, int x1, int y1, unsigned flags, char const * text, char const * valid)
{
	icon.x0 = x0;
	icon.y0 = y0;
	icon.x1 = x1;
	icon.y1 = y1;
	icon.flags = flags;
	icon.data.ind.buffer = text;
	icon.data.ind.validation = valid;
	icon.data.ind.length = (int)strlen(text) + 1;
}


/*
** "About this program", laid out as Edit's progInfo template: right-aligned labels (Name,
** Purpose, Author, Ported by, Version) beside sunken display fields.
*/
void Create_Info_Window(void)
{
	static char version[64];
	snprintf(version, sizeof(version), "%s (%s, %.10s)", OPENTS_VERSION, OPENTS_COMMIT, OPENTS_COMMIT_DATE);

	static struct {
		int visible[4];
		int scroll[2];
		int behind;
		unsigned flags;
		unsigned char colours[8];
		int extent[4];
		unsigned title_flags;
		unsigned button_type;
		int sprite_area;
		short min_w, min_h;
		struct {
			char const * buffer;
			char const * validation;
			int length;
		} title;
		int icon_count;
		Icon icons[10];
	} block;

	static char const title[] = "About this program";
	static char const field_valid[] = "R2";

	memset(&block, 0, sizeof(block));
	block.visible[0] = 392;
	block.visible[1] = 628;
	block.visible[2] = 392 + 668;
	block.visible[3] = 628 + 308;
	block.behind = -1;
	// Moveable, auto-redraw, title bar, new format (as Edit's), but kept on the screen: opened
	// from the icon bar's right-hand end, it would otherwise run off it.
	block.flags = 0x84000012;
	unsigned char const colours[8] = {7, 2, 7, 1, 12, 14, 12, 0};
	memcpy(block.colours, colours, sizeof(colours));
	block.extent[1] = -308;
	block.extent[2] = 668;
	block.title_flags = 0x0000013D; // text, border, centred, filled, indirected
	block.sprite_area = 1;
	block.title.buffer = title;
	block.title.validation = (char const *)-1;
	block.title.length = sizeof(title);

	// Edit's rows and fields, with a wider label column for "Ported by".
	char const * labels[5] = {"Name", "Purpose", "Author", "Ported by", "Version"};
	char const * values[5] = {AppName, AppPurpose, AppAuthor, "David Sharp", version};
	for (int row = 0; row < 5; row++) {
		int const top = -4 - 60 * row;
		Indirected(block.icons[row], 184, top - 52, 660, top, 0x1700613D, values[row], field_valid);
		Plain_Text(block.icons[5 + row], 8, top - 48, 184, top - 8, 0x17000211, labels[row]);
	}
	block.icon_count = 10;

	_kernel_swi_regs regs;
	regs.r[1] = (int)&block;
	if (Swi(Wimp_CreateWindow, regs)) {
		InfoWindow = regs.r[0];
	}
}


/*
** The icon bar menu: Info (leading to the Info window) and Quit.
*/
struct {
	char title[12];
	unsigned char colours[4];
	int width, height, gap;
	struct {
		unsigned flags;
		int submenu;
		unsigned icon_flags;
		char text[12];
	} items[2];
} Menu;


void Build_Menu(void)
{
	memset(&Menu, 0, sizeof(Menu));
	strncpy(Menu.title, AppName, sizeof(Menu.title));
	Menu.colours[0] = 7;
	Menu.colours[1] = 2;
	Menu.colours[2] = 7;
	Menu.colours[3] = 0;
	Menu.width = 128;
	Menu.height = 44;

	Menu.items[0].submenu = InfoWindow;
	Menu.items[0].icon_flags = 0x07000021; // text, filled, black on white
	strncpy(Menu.items[0].text, "Info", sizeof(Menu.items[0].text));

	Menu.items[1].flags = 0x80; // the last item
	Menu.items[1].submenu = -1;
	Menu.items[1].icon_flags = 0x07000021;
	strncpy(Menu.items[1].text, "Quit", sizeof(Menu.items[1].text));
}


void Open_Menu(int mouse_x)
{
	_kernel_swi_regs regs;
	regs.r[1] = (int)&Menu;
	regs.r[2] = mouse_x - 64;
	regs.r[3] = 96 + 2 * 44; // an icon bar menu sits just above the icon bar
	Swi(Wimp_CreateMenu, regs);
}


void Put_Icon_On_Bar(void)
{
	struct {
		int window;
		Icon icon;
	} block;
	block.window = -1; // the right of the icon bar, with the applications
	Plain_Text(block.icon, 0, 0, 68, 68, 0x1700301A, AppSprite); // sprite, centred, click
	_kernel_swi_regs regs;
	regs.r[0] = 0;
	regs.r[1] = (int)&block;
	if (Swi(Wimp_CreateIcon, regs)) {
		IconHandle = regs.r[0];
	}
}


void Give_Screen_To_Desktop(void);


void Quit_Game(void)
{
	fprintf(stderr, "desktop: quit from the icon bar\n");
	Audio_SDL_Pause_For_Desktop(false);
	// The game's own way out (as its queue does): the sound and window shut down first, which
	// a plain exit skipped, crashing on the way out. The Wimp then closes the task.
	Emergency_Exit();
	exit(0); // At_Exit gives the screen back
}


/*
** Polls the Wimp until the icon is clicked with Select or Adjust, handling the icon's menu, the
** Info window and the desktop quitting.
*/
void Wait_For_Icon_Click(void)
{
	int block[64];
	for (;;) {
		__pthread_stop_ticker(); // the game's threads mustn't run while other tasks are paged in
		_kernel_swi_regs regs;
		regs.r[0] = 1; // no null events: sleep until something happens
		regs.r[1] = (int)block;
		_kernel_oserror * error = _kernel_swi(Wimp_Poll, &regs, &regs);
		__pthread_start_ticker();
		if (error != nullptr) {
			fprintf(stderr, "desktop: Wimp_Poll: %s\n", error->errmess);
			return;
		}

		switch (regs.r[0]) {
			case 2: // Open_Window_Request
				regs.r[1] = (int)block;
				Swi(Wimp_OpenWindow, regs);
				break;

			case 3: // Close_Window_Request
				regs.r[1] = (int)block;
				Swi(Wimp_CloseWindow, regs);
				break;

			case 6: // Mouse_Click: x, y, buttons, window, icon
				if (block[3] == -2 && block[4] == IconHandle) {
					if (block[2] & 2) {
						Open_Menu(block[0]);
					} else if (block[2] & (4 | 1)) {
						return;
					}
				}
				break;

			case 9: // Menu_Selection
				if (block[0] == 1) {
					Quit_Game();
				}
				{
					// Adjust keeps the menu open.
					int pointer[5];
					regs.r[1] = (int)pointer;
					if (Swi(Wimp_GetPointerInfo, regs) && (pointer[2] & 1)) {
						Open_Menu(pointer[0] + 64);
					}
				}
				break;

			case 17: // User_Message
			case 18: // User_Message_Recorded
				if (block[4] == 0) { // Message_Quit
					Quit_Game();
				}
				break;

			default:
				break;
		}
	}
}


int Mode_Variable(int variable)
{
	_kernel_swi_regs regs;
	regs.r[0] = -1;
	regs.r[1] = variable;
	_kernel_swi(OS_ReadModeVariable, &regs, &regs);
	return(regs.r[2]);
}


/*
** The screen back to the desktop: the pointer shown and free to go anywhere, Escape working, and
** the whole screen redrawn by its windows.
*/
void Give_Screen_To_Desktop(void)
{
	_kernel_osbyte(229, 0, 0); // Escape as normal
	_kernel_osbyte(106, 1, 0); // the pointer on

	int const right = (Mode_Variable(11) + 1) << Mode_Variable(4);
	int const top = (Mode_Variable(12) + 1) << Mode_Variable(5);
	unsigned char box[9] = {1, 0, 0, 0, 0,
		(unsigned char)(right - 1), (unsigned char)((right - 1) >> 8),
		(unsigned char)(top - 1), (unsigned char)((top - 1) >> 8)};
	_kernel_osword(21, (int *)box);

	_kernel_swi_regs regs;
	regs.r[0] = -1; // the whole screen
	regs.r[1] = 0;
	regs.r[2] = 0;
	regs.r[3] = right;
	regs.r[4] = top;
	Swi(Wimp_ForceRedraw, regs);
}


/*
** The screen to the game again: Escape is a key (as SDL set it), the pointer as SDL has it, and
** keys pressed on the desktop forgotten. The game draws every frame in full, so the next one
** covers the desktop.
*/
void Take_Screen_Back(void)
{
	_kernel_osbyte(229, 1, 0);
	if (!SDL_CursorVisible()) {
		_kernel_osbyte(106, 0, 0);
	}
	_kernel_osbyte(21, 0, 0); // flush the keyboard buffer
}


/*
** However the game ends, the screen it drew over goes back to the desktop: as a Wimp task (not a
** single-tasking program, after which the desktop redraws itself) it must ask for that.
*/
void At_Exit(void)
{
	Give_Screen_To_Desktop();
}

}


void RISCOS_Desktop_Start(bool wait)
{
	if (TaskHandle != 0) {
		return;
	}

	int messages[1] = {0}; // just Message_Quit, which is always delivered
	_kernel_swi_regs regs;
	regs.r[0] = 380;
	regs.r[1] = 0x4B534154; // "TASK"
	regs.r[2] = (int)AppName;
	regs.r[3] = (int)messages;
	if (!Swi(Wimp_Initialise, regs)) {
		return;
	}
	TaskHandle = regs.r[1];
	atexit(At_Exit); // registered before the game's own, so it runs after them

	Create_Info_Window();
	Build_Menu();
	Put_Icon_On_Bar();
	if (IconHandle >= 0 && wait && getenv("OPENTS_NODESKTOP") == nullptr) {
		fprintf(stderr, "desktop: waiting for a click on the icon bar\n");
		Wait_For_Icon_Click();
	}
}


bool RISCOS_Desktop_Is_Leave_Key(unsigned sdl_key, unsigned sdl_mod)
{
	return(sdl_key == SDLK_F12 && (sdl_mod & SDL_KMOD_SHIFT) != 0 && IconHandle >= 0);
}


void RISCOS_Desktop_Suspend(void)
{
	if (IconHandle < 0) {
		return;
	}
	fprintf(stderr, "desktop: left for the desktop\n");
	Audio_SDL_Pause_For_Desktop(true);
	Give_Screen_To_Desktop();
	Wait_For_Icon_Click();
	Take_Screen_Back();
	Audio_SDL_Pause_For_Desktop(false);
	fprintf(stderr, "desktop: back to the game\n");
}
