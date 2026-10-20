// Host-side check of the frame rate counter's font: draws several numbers at several scales over
// a bright and a dark background into a memory buffer and writes it as a PPM (convert to PNG to look).
//   clang++ -std=c++20 -I port/posix port/tools/fpsfont_test.cpp port/posix/fpsfont.cpp -o fpsfont_test
//   ./fpsfont_test out.ppm
#include "fpscounter.h"

#include <cstdio>
#include <vector>

int main(int argc, char ** argv)
{
	int const heights[] = {480, 800, 1050, 1600};
	int const widths[] = {640, 1280, 1680, 2560};
	int const numbers[] = {60, 7, 144, 1000};
	int const cellw = 120, cellh = 60;
	int const width = cellw * 4, height = cellh * 4 * 2;
	std::vector<std::uint32_t> picture((size_t)width * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			// Top half: a bright, busy background; bottom half: a dark one.
			bool const bright = y < height / 2;
			picture[(size_t)y * width + x] = bright ? (((x / 7 + y / 5) & 1) ? 0xFFFFFFFFu : 0xFFE0C080u) : 0xFF101830u;
		}
	}
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			int const unit = FPS_Font_Scale(heights[row]);
			for (int half = 0; half < 2; half++) {
				int const right = (column + 1) * cellw - 4;
				int const bottom = half * (height / 2) + (row + 1) * cellh - 4;
				FPS_Draw_Number(picture.data(), width, width, height, 16, 0, numbers[column], unit, right, bottom);
			}
		}
		std::printf("screen %dx%d: font scale %d\n", widths[row], heights[row], FPS_Font_Scale(heights[row]));
	}
	FILE * file = std::fopen(argc > 1 ? argv[1] : "fpsfont.ppm", "wb");
	std::fprintf(file, "P6\n%d %d\n255\n", width, height);
	for (std::uint32_t pixel : picture) {
		unsigned char rgb[3] = {(unsigned char)(pixel >> 16), (unsigned char)(pixel >> 8), (unsigned char)pixel};
		std::fwrite(rgb, 1, 3, file);
	}
	std::fclose(file);
	return(0);
}
