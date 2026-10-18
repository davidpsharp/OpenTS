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
** "Info" leading to "About this program" with Name, Purpose, Author and Version, "Help", which
** opens !Help, then "Quit".
**
** The game data comes from the freeware CD images (ISO files), dragged onto the icon. !Run checks
** the installed files (Utils.tsprep -check) and leaves any problem in OpenTS$DataProblem; while
** there is one, a click on the icon says how to install the data and opens !Help, instead of
** starting the game. Nothing of the game's has been set up by then (the wait is before WinMain).
** A dragged file is identified here from its directory (discid.h, shared with the Vanilla
** Conquer port): a Tiberian Sun disc is installed by Utils.tsprep in a task window, which only
** opens if something goes wrong; another game's disc, a cut-short image or anything else is
** turned away, saying why. Messages are in a window of the game's own: on RISC OS 4 and 5 an error
** box from a task like this left the desktop unredrawn after it closed (seen in the Vanilla
** Conquer port).
*/

#include "riscosdesktop.h"
#include "tools/discid.h"
#include "riscosoverlay.h"
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
#include <string>
#include <vector>

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
	} items[3];
} Menu;

enum {
	MenuInfo,
	MenuHelp,
	MenuQuit,
};


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

	Menu.items[MenuHelp].submenu = -1;
	Menu.items[MenuHelp].icon_flags = 0x07000021;
	strncpy(Menu.items[MenuHelp].text, "Help", sizeof(Menu.items[MenuHelp].text));

	Menu.items[MenuQuit].flags = 0x80; // the last item
	Menu.items[MenuQuit].submenu = -1;
	Menu.items[MenuQuit].icon_flags = 0x07000021;
	strncpy(Menu.items[MenuQuit].text, "Quit", sizeof(Menu.items[MenuQuit].text));
}


void Open_Menu(int mouse_x)
{
	_kernel_swi_regs regs;
	regs.r[1] = (int)&Menu;
	regs.r[2] = mouse_x - 64;
	regs.r[3] = 96 + 3 * 44; // an icon bar menu sits just above the icon bar
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
** While the game waits on the desktop its memory is paged out, but the handlers UnixLib
** installed stay. An error raised by another task (seen: the Filer's) or an enabled event (a key
** or mouse button press) then ran whatever that task had at the game's addresses, in SVC mode,
** and could corrupt the system heap. Around Wimp_Poll the handlers are the OS's defaults: the
** exceptions, error, callback, escape and event. The game's are put back after.
*/
struct HandlerType
{
	int Address;
	int Workspace;
	int Buffer;
};

// OS_ChangeEnvironment numbers: undefined instruction, prefetch abort, data abort, address
// exception, error, callback, escape, event.
static const int Swapped_Handlers[] = {1, 2, 3, 4, 6, 7, 9, 10};
static const int Swapped_Count = sizeof(Swapped_Handlers) / sizeof(Swapped_Handlers[0]);

static HandlerType Set_Handler(int number, const HandlerType& handler)
{
	_kernel_swi_regs regs;
	regs.r[0] = number;
	regs.r[1] = handler.Address;
	regs.r[2] = handler.Workspace;
	regs.r[3] = handler.Buffer;
	_kernel_swi(OS_ChangeEnvironment, &regs, &regs);
	HandlerType previous = {regs.r[1], regs.r[2], regs.r[3]};
	return previous;
}

// Puts the OS's default handlers in, keeping the game's in saved.
static void Use_Default_Handlers(HandlerType saved[])
{
	for (int i = 0; i < Swapped_Count; i++) {
		_kernel_swi_regs regs;
		regs.r[0] = Swapped_Handlers[i];
		_kernel_swi(OS_ReadDefaultHandler, &regs, &regs);
		HandlerType handler = {regs.r[1], regs.r[2], regs.r[3]};
		saved[i] = Set_Handler(Swapped_Handlers[i], handler);
	}
}

static void Use_Game_Handlers(const HandlerType saved[])
{
	for (int i = 0; i < Swapped_Count; i++) {
		Set_Handler(Swapped_Handlers[i], saved[i]);
	}
}


/*
** The game data: whether it's installed, and installing it from CD images dragged onto the icon.
*/
bool Installing = false;
int InstallTask = 0;         // the task window's task, once seen
unsigned InstallStarted = 0; // when it was started, in centiseconds
std::vector<std::string> Pending; // images dropped, waiting for the rest of the drag
unsigned PendingSince = 0;

unsigned Now(void)
{
	_kernel_swi_regs regs;
	_kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
	return((unsigned)regs.r[0]);
}

// A system variable's value ("" if unset), read from the OS.
std::string Os_Variable(char const * name)
{
	char buf[512];
	_kernel_swi_regs regs;
	regs.r[0] = (int)name;
	regs.r[1] = (int)buf;
	regs.r[2] = sizeof(buf) - 1;
	regs.r[3] = 0;
	regs.r[4] = 3; // expanded to a string
	if (_kernel_swi(OS_ReadVarVal, &regs, &regs) != nullptr) {
		return("");
	}
	buf[regs.r[2]] = '\0';
	return(buf);
}

void Set_Os_Variable(char const * name, char const * value)
{
	_kernel_swi_regs regs;
	regs.r[0] = (int)name;
	regs.r[1] = (int)value;
	regs.r[2] = value != nullptr ? (int)strlen(value) : -1; // -1 deletes it
	regs.r[3] = 0;
	regs.r[4] = 4;
	_kernel_swi(OS_SetVarVal, &regs, &regs);
}

// The problem with the installed data that !Run found (or tsprep, after installing), or "".
std::string Data_Problem(void)
{
	if (!Os_Variable("OpenTS$NoDataCheck").empty()) {
		return("");
	}
	return(Os_Variable("OpenTS$DataProblem"));
}

void Open_Help(void)
{
	std::string command = "Filer_Run " + Os_Variable("OpenTS$Dir") + ".!Help";
	_kernel_swi_regs regs;
	regs.r[0] = (int)command.c_str();
	Swi(OS_CLI, regs);
}

/*
** The message window: the text, the Wimp's "error" or "information" sprite, one button. Return
** or Escape closes it too. The install's progress goes on its last line.
*/
int const MessageWidth = 1040;
int const MaxLines = 9;
int MessageWindow = -1;
int ProgressIcon = -1;
char MessageLines[MaxLines][100];
char ButtonText[20];
char SpriteName[16];

void Close_Message(void)
{
	if (MessageWindow < 0) {
		return;
	}
	int block[1] = {MessageWindow};
	_kernel_swi_regs regs;
	regs.r[1] = (int)block;
	Swi(Wimp_DeleteWindow, regs);
	MessageWindow = -1;
}

int Wrap(std::string const & text, size_t width)
{
	int lines = 0;
	size_t pos = 0;
	while (pos < text.size() && lines < MaxLines) {
		size_t end = text.size();
		if (end - pos > width) {
			end = text.rfind(' ', pos + width);
			if (end == std::string::npos || end <= pos) {
				end = pos + width;
			}
		}
		snprintf(MessageLines[lines++], sizeof(MessageLines[0]), "%s", text.substr(pos, end - pos).c_str());
		pos = end;
		while (pos < text.size() && text[pos] == ' ') {
			pos++;
		}
	}
	return(lines);
}

bool Wimp_Has_Sprite(char const * name)
{
	_kernel_swi_regs regs;
	regs.r[0] = 40; // read sprite information
	regs.r[2] = (int)name;
	return(_kernel_swi(Wimp_SpriteOp, &regs, &regs) == nullptr);
}

void Show_Message(std::string const & text, bool error, char const * button, bool progress = false)
{
	Close_Message();
	fprintf(stderr, "desktop: message: %s\n", text.c_str());
	int lines = Wrap(text, 54);
	ProgressIcon = -1;
	if (progress && lines < MaxLines) {
		MessageLines[lines++][0] = '\0';
	}
	snprintf(SpriteName, sizeof(SpriteName), "%s", error ? "error" : "information");
	bool sprite = Wimp_Has_Sprite(SpriteName);
	int text_x = sprite ? 128 : 32;
	int button_y = -(32 + 44 * (lines > 2 ? lines : 2) + 28);
	int height = -button_y + 68 + 28;

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
		Icon icons[2 + MaxLines];
	} block;
	memset(&block, 0, sizeof(block));
	block.behind = -1;
	block.flags = 0x84000012; // moveable, auto-redraw, title bar, new format; kept on the screen
	unsigned char const colours[8] = {7, 2, 7, 1, 12, 14, 12, 0};
	memcpy(block.colours, colours, sizeof(colours));
	block.extent[1] = -height;
	block.extent[2] = MessageWidth;
	block.title_flags = 0x0000013D;
	block.sprite_area = 1;
	block.title.buffer = AppName;
	block.title.validation = (char const *)-1;
	block.title.length = sizeof(AppName);

	int n = 0;
	static char const default_valid[] = "R6,3";
	snprintf(ButtonText, sizeof(ButtonText), "%s", button);
	Indirected(block.icons[n++], MessageWidth - 32 - 216, button_y - 68, MessageWidth - 32, button_y, 0x1700313D,
		ButtonText, default_valid);
	for (int i = 0; i < lines; i++) {
		int top = -32 - 44 * i;
		if (progress && i == lines - 1) {
			ProgressIcon = n;
		}
		Indirected(block.icons[n++], text_x, top - 44, MessageWidth - 32, top, 0x07000111, MessageLines[i],
			(char const *)-1);
		block.icons[n - 1].data.ind.length = sizeof(MessageLines[i]);
	}
	if (sprite) {
		Indirected(block.icons[n++], 32, -32 - 76, 32 + 76, -32, 0x1700011A, SpriteName, (char const *)1);
		block.icons[n - 1].data.ind.length = (int)strlen(SpriteName) + 1;
	}
	block.icon_count = n;

	_kernel_swi_regs regs;
	regs.r[1] = (int)&block;
	if (!Swi(Wimp_CreateWindow, regs)) {
		return;
	}
	MessageWindow = regs.r[0];

	int size[4];
	static int const vars[] = {4, 5, 11, 12, -1};
	regs.r[0] = (int)vars;
	regs.r[1] = (int)size;
	int screen_w = 1600, screen_h = 1200;
	if (_kernel_swi(OS_ReadVduVariables, &regs, &regs) == nullptr) {
		screen_w = (size[2] + 1) << size[0];
		screen_h = (size[3] + 1) << size[1];
	}
	int open[8] = {MessageWindow, (screen_w - MessageWidth) / 2, (screen_h - height) / 2, 0, 0, 0, 0, -1};
	open[3] = open[1] + MessageWidth;
	open[4] = open[2] + height;
	regs.r[1] = (int)open;
	Swi(Wimp_OpenWindow, regs);
	regs.r[0] = MessageWindow;
	regs.r[1] = -1;
	regs.r[2] = 0;
	regs.r[3] = 0;
	regs.r[4] = 1 << 25; // an invisible caret, so the window gets the keys
	regs.r[5] = -1;
	Swi(Wimp_SetCaretPosition, regs);
}

void Show_Progress(std::string const & text)
{
	if (MessageWindow < 0 || ProgressIcon < 0) {
		return;
	}
	struct {
		int window, icon;
		Icon info;
	} state;
	state.window = MessageWindow;
	state.icon = ProgressIcon;
	_kernel_swi_regs regs;
	regs.r[1] = (int)&state;
	if (!Swi(Wimp_GetIconState, regs)) {
		return;
	}
	char * buffer = const_cast<char *>(state.info.data.ind.buffer);
	if (strcmp(buffer, text.c_str()) == 0) {
		return;
	}
	snprintf(buffer, sizeof(MessageLines[0]), "%s", text.c_str());
	int set[4] = {MessageWindow, ProgressIcon, 0, 0}; // no change to its flags: just redraw it
	regs.r[1] = (int)set;
	Swi(Wimp_SetIconState, regs);
}

void Tell(std::string const & text, bool error)
{
	Show_Message(text, error, "OK");
}

// A click on the icon while the data isn't there.
void Explain_No_Data(void)
{
	if (Installing) {
		Tell("The game data is being installed. Click the icon when that has finished.", false);
		return;
	}
	Tell("Tiberian Sun's game data isn't installed, or is incomplete (" + Data_Problem()
			+ "). Drag the CD images (ISO files) onto this icon to install it. !Help, opening now, says "
			  "where to get them.",
		false);
	Open_Help();
}

// Message_DataLoad on the icon: a file dragged onto it from the Filer.
void Data_Load(int * block)
{
	std::string file = (char const *)block + 44;
	int filetype = block[10];
	std::string leaf = file.substr(file.find_last_of('.') + 1);

	block[3] = block[2]; // DataLoadAck: the file is only read, never moved
	block[4] = 4;
	_kernel_swi_regs regs;
	regs.r[0] = 17;
	regs.r[1] = (int)block;
	regs.r[2] = block[1];
	Swi(Wimp_SendMessage, regs);

	fprintf(stderr, "desktop: %s dragged to the icon\n", file.c_str());
	if (filetype == 0x1000 || filetype == 0x2000) {
		Tell(leaf + " is a directory. Drag the CD image files (ISO files) themselves onto the icon.", true);
		return;
	}
	if (Installing) {
		Tell("The game data is being installed. Drag more CD images here when that has finished.", false);
		return;
	}
	// Any filetype is taken: a disc is known by its contents. Zipped images aren't read yet.
	if (filetype == 0xDDC) {
		Tell(leaf + " is a zip file. Unzip it first, then drag the CD image (ISO file) inside it onto the icon.", true);
		return;
	}
	int cut = 0;
	char volume[40];
	int disc = Disc_Identify(file.c_str(), &cut, volume, sizeof(volume));
	fprintf(stderr, "desktop: %s is %s%s (volume %s)\n", leaf.c_str(),
		disc >= 0 ? Disc_Types[disc].id : disc == DISC_NOT_ISO ? "not a CD image" : "an unknown disc",
		cut ? ", cut short" : "", volume);
	if (disc == DISC_NOT_ISO) {
		Tell(leaf + " isn't a CD image (ISO file). !Help says which ones to drag here.", true);
	} else if (cut) {
		Tell(leaf + " is " + (disc >= 0 ? Disc_Types[disc].name : std::string("a CD image"))
				+ ", but cut short: its download or copy didn't finish. Get it again.",
			true);
	} else if (disc == DISC_UNKNOWN) {
		Tell(leaf + " isn't one of the freeware discs this game installs from. !Help says which ones to get.", true);
	} else if (Disc_Types[disc].app == nullptr) {
		Tell(leaf + " is " + Disc_Types[disc].name + ", which isn't one this or any other game here uses.", true);
	} else if (strcmp(Disc_Types[disc].app, "!OpenTS") != 0) {
		Tell(leaf + " is " + Disc_Types[disc].name + ": drag it onto the " + Disc_Types[disc].app + " icon instead.",
			true);
	} else {
		Pending.push_back(file);
		PendingSince = Now();
	}
}

std::string const InstallTaskName = "OpenTS install";

// Installs the images in Pending with Utils.tsprep in a task window; the list goes in OpenTS$Install.
void Start_Install(void)
{
	std::string list;
	for (size_t i = 0; i < Pending.size(); i++) {
		list += Pending[i] + "\n";
	}
	Pending.clear();
	Set_Os_Variable("OpenTS$Install", list.c_str());
	std::string dir = Os_Variable("OpenTS$Dir");
	std::string prepare = dir + ".Utils.tsprep -window -images OpenTS$Install -dest " + dir;
	if (!Os_Variable("OpenTS$NoMovies").empty()) {
		prepare += " -nomovies";
	}
	std::string command = "TaskWindow \"" + prepare + "\" -wimpslot 2000K -name \"" + InstallTaskName + "\" -quit";
	fprintf(stderr, "desktop: %s\n", command.c_str());
	_kernel_swi_regs regs;
	regs.r[0] = (int)command.c_str();
	_kernel_oserror * error = _kernel_swi(Wimp_StartTask, &regs, &regs);
	if (error != nullptr) {
		Tell(std::string("The installer couldn't be started in a task window: ") + error->errmess, true);
		return;
	}
	Installing = true;
	InstallTask = 0;
	InstallStarted = Now();
	Show_Message("Installing the Tiberian Sun game data from the CD image. This takes a few minutes per disc; "
				 "the desktop carries on meanwhile.",
		false, "Hide", true);
	Show_Progress("Starting");
}

// The task window's task if it's running, or 0.
int Find_Install_Task(void)
{
	int buffer[16 * 4];
	_kernel_swi_regs regs;
	regs.r[0] = 0;
	do {
		regs.r[1] = (int)buffer;
		regs.r[2] = sizeof(buffer);
		if (_kernel_swi(TaskManager_EnumerateTasks, &regs, &regs) != nullptr) {
			return(0);
		}
		int entries = ((int *)regs.r[1] - buffer) / 4;
		for (int i = 0; i < entries; i++) {
			char const * task = (char const *)buffer[i * 4 + 1];
			if (task != nullptr && strncmp(task, InstallTaskName.c_str(), InstallTaskName.size()) == 0
				&& (unsigned char)task[InstallTaskName.size()] < ' ') {
				return(buffer[i * 4]);
			}
		}
	} while (regs.r[0] >= 0);
	return(0);
}

void Install_Finished(void)
{
	Installing = false;
	InstallTask = 0;
	std::string problem = Data_Problem();
	fprintf(stderr, "desktop: install finished: %s\n", problem.empty() ? "all there" : problem.c_str());
	if (problem.empty()) {
		Tell("The Tiberian Sun game data is installed. Click the icon to play.", false);
	} else {
		Tell("Tiberian Sun's game data is still incomplete (" + problem
				+ "). The install window said why. Drag the CD images onto this icon to try again.",
			true);
	}
}

// On a null event while installing, or while a drag is still arriving.
void Install_Tick(void)
{
	if (!Pending.empty() && !Installing && Now() - PendingSince >= 50) {
		Start_Install();
		return;
	}
	if (Installing) {
		std::string now = Os_Variable("OpenTS$Installing");
		if (!now.empty()) {
			Show_Progress("Now: " + now);
		}
		int task = Find_Install_Task();
		if (task != 0) {
			InstallTask = task;
		} else if (InstallTask != 0 || Now() - InstallStarted > 3000) {
			Install_Finished();
		}
	}
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
		bool ticking = Installing || !Pending.empty();
		regs.r[0] = ticking ? 0 : 1; // null events only to watch an install: else sleep
		regs.r[1] = (int)block;
		regs.r[2] = (int)(Now() + 50);
		HandlerType game_handlers[Swapped_Count];
		Use_Default_Handlers(game_handlers);
		_kernel_oserror * error = _kernel_swi(ticking ? Wimp_PollIdle : Wimp_Poll, &regs, &regs);
		Use_Game_Handlers(game_handlers);
		__pthread_start_ticker();
		if (error != nullptr) {
			fprintf(stderr, "desktop: Wimp_Poll: %s\n", error->errmess);
			return;
		}

		switch (regs.r[0]) {
			case 0: // Null_Reason
				Install_Tick();
				break;

			case 2: // Open_Window_Request
				regs.r[1] = (int)block;
				Swi(Wimp_OpenWindow, regs);
				break;

			case 3: // Close_Window_Request
				if (block[0] == MessageWindow) {
					Close_Message();
					break;
				}
				regs.r[1] = (int)block;
				Swi(Wimp_CloseWindow, regs);
				break;

			case 8: // Key_Pressed: the message window has the caret
				if (block[0] == MessageWindow && (block[6] == 13 || block[6] == 27)) {
					Close_Message();
					break;
				}
				regs.r[0] = block[6];
				Swi(Wimp_ProcessKey, regs);
				break;

			case 6: // Mouse_Click: x, y, buttons, window, icon
				if (block[3] == MessageWindow && block[4] == 0 && (block[2] & (4 | 1))) {
					Close_Message();
					break;
				}
				if (block[3] == -2 && block[4] == IconHandle) {
					if (block[2] & 2) {
						Open_Menu(block[0]);
					} else if (block[2] & (4 | 1)) {
						if (Installing || !Data_Problem().empty()) {
							Explain_No_Data();
							break;
						}
						Close_Message();
						return;
					}
				}
				break;

			case 9: // Menu_Selection
				if (block[0] == MenuQuit) {
					Quit_Game();
				}
				if (block[0] == MenuHelp) {
					Open_Help();
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
				if (block[4] == 3 && block[5] == -2 && block[6] == IconHandle) { // Message_DataLoad
					Data_Load(block);
				}
				if (block[4] == 0x400C3 && Installing && InstallTask != 0 && block[1] == InstallTask) {
					Install_Finished(); // Message_TaskCloseDown from the task window
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
	RISCOS_Overlay_Leave(); // the overlay covers everything
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

	// Files dragged to the icon, and task windows closing (Message_Quit always comes).
	int messages[3] = {3, 0x400C3, 0};
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
	// Without its data the game can't start: it waits for the CD images, whatever else was asked.
	bool no_data = !Data_Problem().empty();
	if (no_data) {
		fprintf(stderr, "desktop: no game data (%s)\n", Data_Problem().c_str());
	}
	// OPENTS_NODESKTOP (for tests) skips the wait, but never when there is no data to play with.
	if (IconHandle >= 0 && (no_data || (wait && getenv("OPENTS_NODESKTOP") == nullptr))) {
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
