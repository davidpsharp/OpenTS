// Host-side check of the VQ block decoders' destination alignment. Each decoder that stores whole
// words is run on random codebook and pointer data into a destination at byte offsets 0, 2, 4
// and 6 (the movie can sit at any pixel column, so only 2-byte alignment is guaranteed), and the
// result must be the same picture each time. With -fsanitize=alignment, a word store to a
// destination that isn't 4-aligned is reported. Optionally the same decoders from an older
// unvq.cpp (compiled with every UnVQ name prefixed Old_) are compared byte for byte at offset 0,
// and the time per call of each is printed.
//   clang++ -std=c++17 -O2 -fsanitize=alignment code/vqalib/unvq.cpp port/tools/unvq_align_test.cpp -o unvq_align_test
//   (older copy: clang++ -c -O2 -DOLD -Dxx... see OLD_DEFS below)
//   ./unvq_align_test
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define __cdecl
typedef void (*Decoder)(uint8_t *, uint8_t *, uint8_t *, size_t, size_t, size_t);

#define DECLARE(n) void n(uint8_t *, uint8_t *, uint8_t *, size_t, size_t, size_t);
#define LIST(X) X(UnVQ2_C1_4x4) X(UnVQ1_C4_4x4) X(UnVQ2_C4_4x4) X(UnVQ1_C4_4x2) X(UnVQ2_C4_4x2)
LIST(DECLARE)
#ifdef HAVE_OLD
#define OLDDECLARE(n) void Old_##n(uint8_t *, uint8_t *, uint8_t *, size_t, size_t, size_t);
LIST(OLDDECLARE)
#endif

static uint32_t Crc(uint8_t const * p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}
	return ~c;
}

int main()
{
	struct { const char * name; Decoder fn, old; } const decoders[] = {
#ifdef HAVE_OLD
#define ENTRY(n) {#n, n, Old_##n},
#else
#define ENTRY(n) {#n, n, nullptr},
#endif
		LIST(ENTRY)
	};
	size_t const blocksperrow = 40, numrows = 16, bufwidth = 160; // pixels; 640 x 64 bytes of picture
	size_t const stride = bufwidth * 2, slack = 1 << 21;
	srand(12345);
	std::vector<uint8_t> codebook(1 << 22), pointers(1 << 22);
	for (auto & b : codebook) b = (uint8_t)rand();
	for (auto & b : pointers) b = (uint8_t)rand();
	int failures = 0;
	for (auto const & d : decoders) {
		std::vector<uint32_t> crc;
		for (int off = 0; off <= 6; off += 2) {
			std::vector<uint8_t> raw(stride * numrows * 4 + slack + 16, 0xA5);
			uint8_t * dst = raw.data() + 8 + off;
			// A fresh copy of the stream per call: some decoders edit nothing, but be sure.
			d.fn(codebook.data(), pointers.data(), dst, blocksperrow, numrows, bufwidth);
			crc.push_back(Crc(dst, stride * numrows * 4));
			if (off == 0 && d.old) {
				std::vector<uint8_t> raw2(raw.size(), 0xA5);
				d.old(codebook.data(), pointers.data(), raw2.data() + 8, blocksperrow, numrows, bufwidth);
				if (memcmp(raw.data(), raw2.data(), raw.size()) != 0) {
					printf("%s: output differs from the old decoder\n", d.name);
					failures++;
				}
			}
		}
		bool same = crc[0] == crc[1] && crc[0] == crc[2] && crc[0] == crc[3];
		printf("%-14s crc %08x %08x %08x %08x %s\n", d.name, crc[0], crc[1], crc[2], crc[3], same ? "ok" : "MISMATCH");
		if (!same) failures++;
		// Time per call, aligned destination.
		std::vector<uint8_t> raw(stride * numrows * 4 + slack + 16, 0);
		// Best of several rounds, alternating, so a warm-up or a busy machine doesn't decide it.
		double best[2] = {1e30, 1e30};
		for (int round = 0; round < 7; round++) {
			for (int which = 0; which < 2; which++) {
				Decoder fn = which ? d.old : d.fn;
				if (!fn) continue;
				auto t0 = std::chrono::steady_clock::now();
				int const reps = 1000;
				for (int i = 0; i < reps; i++) fn(codebook.data(), pointers.data(), raw.data() + 8, blocksperrow, numrows, bufwidth);
				double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / reps;
				if (us < best[which]) best[which] = us;
			}
		}
		printf("    new %.2f us per call", best[0]);
		if (d.old) printf(", old %.2f us per call", best[1]);
		printf("\n");
	}
	return failures ? 1 : 0;
}
