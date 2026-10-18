/*
** The speed test's stand-ins for a frame's work (autosize.cpp), kept apart so a test program can
** time them on their own (port/riscos/tools/autobench.cpp). Each runs for about the time it's
** given and says how long one unit of its work took.
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

namespace AutoSizeBench
{

inline std::uint64_t Now_NS(void)
{
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return((std::uint64_t)now.tv_sec * 1000000000u + (std::uint64_t)now.tv_nsec);
}

inline volatile std::uint32_t Sink;	// keeps the work from being optimised away

/*
** Game-like work: going through a table of objects (count of them, 32 bytes each, count a power
** of two) in turn, as the game goes through its units, each looking at another one picked by its
** data and changing with data-dependent branches. Returns ns for 4096 objects.
*/
inline int Time_CPU(std::uint64_t budget, unsigned count)
{
	struct Object { std::uint32_t Health, X, Y, Target, State, Pad[3]; };
	std::vector<Object> objects(count);
	std::uint32_t const mask = count - 1;
	std::uint32_t seed = 12345;
	for (std::size_t i = 0; i < objects.size(); i++) {
		seed = seed * 1103515245u + 12345u;
		objects[i] = {seed >> 20, seed & 0xfff, (seed >> 12) & 0xfff, (seed >> 7) & mask, seed & 7, {0, 0, 0}};
	}
	std::uint64_t const start = Now_NS();
	std::uint32_t at = 0, sum = 0;
	unsigned rounds = 0;
	std::uint64_t now;
	do {
		for (int i = 0; i < 4096; i++, at = (at + 1) & mask) {
			Object & o = objects[at];
			Object & t = objects[o.Target];
			switch (o.State) {
				case 0: o.X += (t.X > o.X) ? 1 : -1; break;
				case 1: o.Y += (t.Y > o.Y) ? 1 : -1; break;
				case 2: if (t.Health > 10) t.Health -= 3; else o.State = 5; break;
				case 3: o.Target = (o.Target * 2654435761u + sum) & mask; break;
				default: o.State = (o.State + o.X + t.Y) & 7; break;
			}
			sum += o.X * 3 + o.Y;
		}
		rounds++;
		now = Now_NS();
	} while (now - start < budget);
	Sink = sum;
	return((int)((now - start) / rounds));
}

/*
** The per-pixel work of a frame of width x height: 8-bit shapes drawn through a palette into the
** 16-bit frame (twice over, as things overlap), the frame converted to 32 bits through the same
** 65536-entry table the game uses, and copied out as it is to the overlay. At least two frames
** are timed, however long they take. Returns ps a pixel.
*/
inline int Time_Pixels(std::uint64_t budget, int width, int height)
{
	int const n = width * height;
	std::vector<std::uint8_t> shapes(n);
	std::vector<std::uint16_t> frame(n), palette(256);
	std::vector<std::uint32_t> converted(n), shown(n), expand(65536);
	std::uint32_t seed = 54321;
	for (int i = 0; i < n; i++) {
		seed = seed * 1103515245u + 12345u;
		shapes[i] = (seed >> 24) < 64 ? 0 : (std::uint8_t)(seed >> 16);	// a quarter see-through
	}
	for (int i = 0; i < 256; i++) {
		palette[i] = (std::uint16_t)(i * 257);
	}
	for (int i = 0; i < 65536; i++) {
		expand[i] = 0xFF000000u | ((i & 0xf800) << 8) | ((i & 0x07e0) << 5) | ((i & 0x1f) << 3);
	}
	std::uint64_t const start = Now_NS();
	unsigned frames = 0;
	std::uint64_t now;
	do {
		for (int pass = 0; pass < 2; pass++) {
			std::uint8_t const * s = shapes.data() + pass * 7;
			for (int i = 0; i < n - 7; i++) {
				std::uint8_t const c = s[i];
				if (c != 0) {
					frame[i] = palette[c];
				}
			}
		}
		for (int i = 0; i < n; i++) {
			converted[i] = expand[frame[i]];
		}
		std::memcpy(shown.data(), converted.data(), n * 4);
		frames++;
		now = Now_NS();
	} while (now - start < budget || frames < 2);
	Sink = shown[frames % n];
	return((int)((now - start) * 1000 / ((std::uint64_t)frames * n)));
}

/*
** Stretching a 640x480 frame to a desktop-sized picture, nearest pixel, as the game does when
** there's no overlay. Returns ps a desktop pixel.
*/
inline int Time_Scale(std::uint64_t budget, int deskwidth, int deskheight)
{
	int const w = deskwidth < 1920 ? deskwidth : 1920, h = deskheight < 1200 ? deskheight : 1200;
	std::vector<std::uint32_t> frame(640 * 480, 0xff336699u), desk((std::size_t)w * h);
	std::uint64_t const start = Now_NS();
	unsigned rounds = 0;
	std::uint64_t now;
	do {
		for (int y = 0; y < h; y++) {
			std::uint32_t const * row = frame.data() + (y * 480 / h) * 640;
			std::uint32_t * out = desk.data() + (std::size_t)y * w;
			unsigned const step = (640u << 16) / (unsigned)w;
			unsigned at = 0;
			for (int x = 0; x < w; x++, at += step) {
				out[x] = row[at >> 16];
			}
		}
		rounds++;
		now = Now_NS();
	} while (now - start < budget);
	Sink = desk[rounds % desk.size()];
	return((int)((now - start) * 1000 / ((std::uint64_t)rounds * w * h)));
}

} // namespace AutoSizeBench
