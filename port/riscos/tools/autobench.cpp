/*
** The automatic screen size's stand-ins on their own, for measuring machines (see
** port/riscos/autosize.cpp): their times at several sizes, without starting the game. Built in the
** opents-riscos image, from the repository:
**   port/riscos/clang++ -O2 -std=c++20 -c port/riscos/tools/autobench.cpp -o ab.o
**   $ENV/bin/arm-riscos-gnueabihf-gcc -O2 -c port/riscos/aeabi.c -o ae.o
**   $ENV/bin/arm-riscos-gnueabihf-g++ -static ab.o ae.o -o ab && $ENV/bin/elf2aif -e ab autobench,ff8
** and run from the command line: *autobench [repeats]
*/
#include "../autosizebench.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char ** argv)
{
	int const repeats = argc > 1 ? atoi(argv[1]) : 3;
	for (int i = 0; i < repeats; i++) {
		std::uint64_t const start = AutoSizeBench::Now_NS();
		int const cpu256k = AutoSizeBench::Time_CPU(300000000, 8192);
		int const cpu2m = AutoSizeBench::Time_CPU(300000000, 65536);
		int const cpu8m = AutoSizeBench::Time_CPU(300000000, 262144);
		int const p640 = AutoSizeBench::Time_Pixels(200000000, 640, 480);
		int const p1024 = AutoSizeBench::Time_Pixels(200000000, 1024, 640);
		int const p1280 = AutoSizeBench::Time_Pixels(200000000, 1280, 800);
		int const p1440 = AutoSizeBench::Time_Pixels(200000000, 1440, 900);
		int const p1680 = AutoSizeBench::Time_Pixels(200000000, 1680, 1050);
		int const scale = AutoSizeBench::Time_Scale(300000000, 1680, 1050);
		printf("cpu256k %d  cpu2m %d  cpu8m %d ns | pixels ps 640x480 %d 1024x640 %d 1280x800 %d 1440x900 %d 1680x1050 %d | scale %d ps (%u ms)\n",
			cpu256k, cpu2m, cpu8m, p640, p1024, p1280, p1440, p1680, scale, (unsigned)((AutoSizeBench::Now_NS() - start) / 1000000));
	}
	return(0);
}
