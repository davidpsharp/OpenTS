/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The size the game draws at, picked for the player the first time. Each frame costs a part that
** doesn't depend on the size (the game itself) and a part that grows with the pixels it draws,
** converts and shows; without the Pi's overlay, stretching to the desktop costs a part for every
** desktop pixel too. A speed test of about a second and a half times small stand-ins for each
** (autosizebench.h). How long a frame takes at each size in a busy game was measured on a Pi 2, 3
** and 4; the nearest of those in speed is taken, its fixed part scaled by the test's game-like
** work and the rest by its pixel work. The size picked is the biggest of the desktop's shape whose
** busiest moments still run faster than 60 frames a second with a little to spare, preferring one
** that divides the desktop exactly (its pixels then stay square and sharp when stretched). When
** none would, it's 640x480, the smallest and fastest.
**
** The results are kept in the [RISCOS] section of the settings, so the test runs only once;
** OpenTS$Retune runs it again. Once the player picks a size in Options > Display it's theirs.
*/

#include "riscosautosize.h"
#include "riscosoverlay.h"
#include "autosizebench.h"

#include "always.h"
#include "ccfile.h"
#include "ccini.h"

#include <kernel.h>
#include <swis.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

char const SECTION[] = "RISCOS";
int const TEST_VERSION = 4;		// a new version of the test runs it again

unsigned const CPU_OBJECTS = 65536;	// 2 MB of them, like a busy game
int const TARGET_FPS = 60;		// the game's normal speed
double const HEADROOM = 1.1;	// the busiest moments must still be this much faster

int Mode_Variable(int variable)
{
	_kernel_swi_regs regs;
	regs.r[0] = -1;
	regs.r[1] = variable;
	_kernel_swi(OS_ReadModeVariable, &regs, &regs);
	return(regs.r[2]);
}

struct Speed
{
	int CPU = 0;		// ns for 4096 objects of the game-like work
	int Pixels = 0;		// ps a game pixel: drawn, converted, copied
	int Scale = 0;		// ps a desktop pixel stretched to
};

/*
** Machines measured: the speed test's results on each, and the slowest 5 seconds of a game there
** (a 2-AI spectator game on Grand Canyon, game speed 0, to game frame 21600, about 6 minutes of
** game time at normal speed, with the whole map in view, through the overlay), as ms a frame at
** each size. Fixed is the part of a frame that doesn't depend on its size (where the line through
** the two smallest sizes meets zero pixels). Desktop is what drawing the usual way (without the
** overlay, OpenTS$NoOverlay set) added to each frame of the same game, in ms for each ms the
** test's stretching would take for the whole desktop: measured at 1024x640 on the Pi 2 and 3 and
** at 1280x800 on the Pi 4, with a 1680x1050 desktop.
*/
struct Point
{
	int Pixels;
	double MS;
};

struct Machine
{
	char const * Name;
	Speed Test;
	double Fixed;
	double Desktop;
	Point Points[5];
};

Machine const Measured[] = {
	{"Raspberry Pi 2", {1115000, 50900, 8040}, 17.14, 2.83,
		{{640 * 480, 27.78}, {1024 * 640, 39.84}, {1280 * 800, 50.00}, {0, 0}, {0, 0}}},
	{"Raspberry Pi 3", {682800, 30960, 3980}, 8.83, 2.52,
		{{640 * 480, 16.18}, {1024 * 640, 24.51}, {1280 * 800, 31.15}, {0, 0}, {0, 0}}},
	{"Raspberry Pi 4", {243000, 9700, 1531}, 3.23, 3.49,
		{{640 * 480, 5.06}, {1280 * 800, 9.33}, {1440 * 900, 13.05}, {1680 * 1050, 18.35}, {0, 0}}},
};

// The measured machine's ms a frame at this many pixels: along its points, and beyond the last
// along the line through the last two.
double Measured_MS(Machine const & machine, int pixels)
{
	int count = 0;
	while (count < 5 && machine.Points[count].Pixels != 0) {
		count++;
	}
	int segment = 0;
	while (segment < count - 2 && pixels > machine.Points[segment + 1].Pixels) {
		segment++;
	}
	Point const & a = machine.Points[segment];
	Point const & b = machine.Points[segment + 1];
	return(a.MS + (b.MS - a.MS) * (double)(pixels - a.Pixels) / (double)(b.Pixels - a.Pixels));
}

// The measured machine nearest in speed to this one.
Machine const & Nearest(Speed const & speed)
{
	Machine const * best = &Measured[0];
	double bestdistance = 1e30;
	for (Machine const & machine : Measured) {
		double const cpu = std::log((double)speed.CPU / machine.Test.CPU);
		double const pixels = std::log((double)speed.Pixels / machine.Test.Pixels);
		double const distance = cpu * cpu + pixels * pixels;
		if (distance < bestdistance) {
			bestdistance = distance;
			best = &machine;
		}
	}
	return(*best);
}

// The busiest moments' frames a second at this size: the nearest measured machine's, its fixed part
// scaled by the test's game-like work and the rest by its pixel work.
double Projected_FPS(Speed const & speed, Machine const & machine, int width, int height, int deskpixels, bool overlay)
{
	double const ms = Measured_MS(machine, width * height);
	double frame = machine.Fixed * speed.CPU / machine.Test.CPU
		+ (ms - machine.Fixed) * speed.Pixels / machine.Test.Pixels;
	if (!overlay) {
		frame += machine.Desktop * speed.Scale * 1.0e-9 * deskpixels;	// ps to ms
	}
	return(1000.0 / frame);
}

struct Size
{
	int Width, Height;
};

// Sizes the game may be given, besides the desktop's own and its halves and thirds.
Size const Sizes[] = {
	{640, 480}, {768, 480}, {854, 480}, {800, 600}, {960, 600}, {1024, 576}, {1024, 600}, {1024, 640},
	{1024, 768}, {1152, 720}, {1152, 864}, {1280, 720}, {1280, 768}, {1280, 800}, {1280, 960},
	{1280, 1024}, {1366, 768}, {1440, 900}, {1600, 900}, {1600, 1000}, {1600, 1200}, {1680, 1050},
	{1920, 1080}, {1920, 1200},
};

bool Retune_Asked(void)
{
	char const * retune = getenv("OpenTS$Retune");
	return(retune != nullptr && retune[0] != '\0' && strcmp(retune, "0") != 0);
}

// OpenTS$Retune asks once: it's deleted once the test has run again.
void Retune_Done(void)
{
	_kernel_swi_regs regs;
	regs.r[0] = (int)"OpenTS$Retune";
	regs.r[1] = 0;
	regs.r[2] = -1;	// delete it
	regs.r[3] = 0;
	regs.r[4] = 0;
	_kernel_swi(OS_SetVarVal, &regs, &regs);
}

} // namespace


void RISCOS_Auto_Size(CCINIClass & ini, char const * settingsfile, int & width, int & height)
{
	// A size other than the one last picked here is the player's (or the settings file's) own,
	// except 640x480 in settings from before there was a pick: the size earlier previews came with.
	int const autowidth = ini.Get_Int(SECTION, "AutoWidth", -1);
	int const autoheight = ini.Get_Int(SECTION, "AutoHeight", -1);
	bool const old_default = autowidth == -1 && width == 640 && height == 480;
	bool const chosen = width > 0 && height > 0 && !(width == autowidth && height == autoheight) && !old_default;
	if (chosen && !Retune_Asked()) {
		fprintf(stderr, "autosize: %dx%d, as chosen in the settings\n", width, height);
		return;
	}

	int const deskwidth = Mode_Variable(11) + 1;
	int const deskheight = Mode_Variable(12) + 1;
	// The desktop's shape on the monitor: pixels can be twice as tall as wide (eigen factors).
	double const aspect = (double)(deskwidth << Mode_Variable(4)) / (double)(deskheight << Mode_Variable(5));
	bool const overlay = RISCOS_Overlay_Available();

	Speed speed;
	bool tested = false;
	if (ini.Get_Int(SECTION, "SpeedTest", 0) == TEST_VERSION && !Retune_Asked()) {
		speed.CPU = ini.Get_Int(SECTION, "SpeedCPU", 0);
		speed.Pixels = ini.Get_Int(SECTION, "SpeedPixels", 0);
		speed.Scale = ini.Get_Int(SECTION, "SpeedScale", 0);
	}
	if (speed.CPU <= 0 || speed.Pixels <= 0 || speed.Scale <= 0) {
		// Each is timed five times and the middle one kept, so a moment when something else took
		// the machine (seen once on a Pi 4: the game work took half as long again) doesn't count,
		// nor does a lucky one.
		// On a Pi 4 the first few tenths of a second just after the game starts are often slower;
		// they're spent warming up and not counted.
		std::uint64_t const start = AutoSizeBench::Now_NS();
		AutoSizeBench::Time_CPU(100000000, CPU_OBJECTS);
		AutoSizeBench::Time_Pixels(100000000, 640, 480);
		int cpu[5], pixels[5], scale[5];
		for (int i = 0; i < 5; i++) {
			cpu[i] = AutoSizeBench::Time_CPU(80000000, CPU_OBJECTS);
			pixels[i] = AutoSizeBench::Time_Pixels(80000000, 640, 480);
			scale[i] = AutoSizeBench::Time_Scale(60000000, deskwidth, deskheight);
		}
		std::sort(cpu, cpu + 5);
		std::sort(pixels, pixels + 5);
		std::sort(scale, scale + 5);
		speed.CPU = cpu[2];
		speed.Pixels = pixels[2];
		speed.Scale = scale[2];
		tested = true;
		fprintf(stderr, "autosize: speed test (%u ms): game work %d ns (%d-%d), pixels %d ps (%d-%d), stretching %d ps (%d-%d)\n",
			(unsigned)((AutoSizeBench::Now_NS() - start) / 1000000), speed.CPU, cpu[0], cpu[4], speed.Pixels, pixels[0], pixels[4],
			speed.Scale, scale[0], scale[4]);
		if (Retune_Asked()) {
			Retune_Done();
		}
	}

	Machine const & machine = Nearest(speed);
	fprintf(stderr, "autosize: projecting from the %s\n", machine.Name);

	// The desktop's own size, its halves and thirds, and the usual sizes of its shape.
	std::vector<Size> candidates;
	for (int divide = 1; divide <= 3; divide++) {
		if (deskwidth % divide == 0 && deskheight % divide == 0) {
			candidates.push_back({deskwidth / divide, deskheight / divide});
		}
	}
	for (Size const & size : Sizes) {
		bool const listed = std::any_of(candidates.begin(), candidates.end(),
			[&](Size const & other) { return(other.Width == size.Width && other.Height == size.Height); });
		if (!listed && std::fabs((double)size.Width / size.Height - aspect) / aspect < 0.02) {
			candidates.push_back(size);
		}
	}

	double const needed = TARGET_FPS * HEADROOM;
	int const deskpixels = deskwidth * deskheight;
	Size best = {0, 0};
	for (Size const & size : candidates) {
		if (size.Width < 640 || size.Height < 480 || size.Width > deskwidth || size.Height > deskheight
			|| size.Width % 4 != 0) {
			continue;
		}
		double const fps = Projected_FPS(speed, machine, size.Width, size.Height, deskpixels, overlay);
		fprintf(stderr, "autosize:   %dx%d: busiest %.0f frames a second\n", size.Width, size.Height, fps);
		if (fps >= needed && size.Width * size.Height > best.Width * best.Height) {
			best = size;
		}
	}
	// A size that divides the desktop exactly, if it's nearly as big, stays sharp when stretched.
	for (int divide = 1; divide <= 3 && best.Width != 0; divide++) {
		Size const exact = {deskwidth / divide, deskheight / divide};
		if (deskwidth % divide == 0 && deskheight % divide == 0 && exact.Width >= 640 && exact.Height >= 480
			&& exact.Width * exact.Height <= best.Width * best.Height
			&& exact.Width * exact.Height * 4 >= best.Width * best.Height * 3) {
			best = exact;
			break;
		}
	}
	// Too slow for any of them to be smooth: the smallest the game draws, the fastest.
	if (best.Width == 0) {
		best = {640, 480};
	}
	fprintf(stderr, "autosize: desktop %dx%d (shape %.3f), %s overlay: %dx%d picked\n", deskwidth, deskheight,
		aspect, overlay ? "with the" : "without the", best.Width, best.Height);

	width = best.Width;
	height = best.Height;
	if (tested || best.Width != autowidth || best.Height != autoheight) {
		ini.Put_Int(SECTION, "SpeedTest", TEST_VERSION);
		ini.Put_Int(SECTION, "SpeedCPU", speed.CPU);
		ini.Put_Int(SECTION, "SpeedPixels", speed.Pixels);
		ini.Put_Int(SECTION, "SpeedScale", speed.Scale);
		ini.Put_Int(SECTION, "AutoWidth", best.Width);
		ini.Put_Int(SECTION, "AutoHeight", best.Height);
		ini.Put_Int("Video", "ScreenWidth", best.Width);
		ini.Put_Int("Video", "ScreenHeight", best.Height);
		CCFileClass file(settingsfile);
		ini.Save(file, false);
	}
}
