/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The report side of -DOPENTS_ALIGNCHECK=ON, in place of the sanitizer runtime's own.
**
** The stock handler has two flaws for finding what a 32-bit ARMv7 machine would fault on. It
** asks for the alignment of the 64-bit Mac type (8 bytes for a pointer or a long, where RISC OS
** needs 4), so it also reports accesses that are fine there. And it reports each source line
** once, so an access that is aligned the first time hides a misaligned one the next time.
**
** This handler reports an access only if its address is not a multiple of min(required, 4), and
** it reports each (line, address modulo 4, kind of access) once.
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

struct SourceLocation {
	const char * Filename;
	uint32_t Line;
	uint32_t Column;
};

struct TypeDescriptor {
	uint16_t Kind;
	uint16_t Info;
	char Name[1];
};

struct TypeMismatchData {
	SourceLocation Loc;
	const TypeDescriptor * Type;
	unsigned char LogAlignment;
	unsigned char TypeCheckKind;
};

const char * const KindNames[] = {"load of", "store to", "reference binding to", "member access within",
	"member call on", "constructor call on", "downcast of", "downcast of", "upcast of", "cast to virtual base of"};

struct Seen {
	const char * File;
	uint32_t Line;
	unsigned Kind;
	unsigned Offset;
};

const int MAX_SEEN = 4096;
Seen SeenTable[MAX_SEEN];
int SeenCount = 0;
unsigned long Faults = 0;
unsigned long Distinct = 0;

void Summary(void)
{
	if (Faults != 0) {
		fprintf(stderr, "ALIGNCHECK: %lu unaligned accesses in %lu distinct places (line, kind, address mod 4)\n", Faults, Distinct);
	} else {
		fprintf(stderr, "ALIGNCHECK: no unaligned accesses\n");
	}
}

void Report(TypeMismatchData * data, uintptr_t ptr)
{
	static bool registered = false;
	if (!registered) {
		registered = true;
		atexit(Summary);
	}

	uintptr_t need = (uintptr_t)1 << data->LogAlignment;
	if (need > 4) {
		need = 4;
	}
	if (ptr == 0 || (ptr & (need - 1)) == 0) {
		return;
	}

	Faults++;
	unsigned kind = data->TypeCheckKind;
	unsigned offset = (unsigned)(ptr & 3);
	for (int i = 0; i < SeenCount; i++) {
		if (SeenTable[i].File == data->Loc.Filename && SeenTable[i].Line == data->Loc.Line &&
			SeenTable[i].Kind == kind && SeenTable[i].Offset == offset) {
			return;
		}
	}
	if (SeenCount < MAX_SEEN) {
		SeenTable[SeenCount++] = Seen{data->Loc.Filename, data->Loc.Line, kind, offset};
	}
	Distinct++;
	fprintf(stderr, "ALIGNCHECK: %s:%u:%u: %s misaligned address %p for type '%s', which needs %u byte alignment (address mod 4 = %u)\n",
		data->Loc.Filename, data->Loc.Line, data->Loc.Column,
		kind < sizeof(KindNames) / sizeof(KindNames[0]) ? KindNames[kind] : "access to", (void *)ptr,
		data->Type->Name, (unsigned)(1u << data->LogAlignment), offset);
}

} // namespace

extern "C" {

void __ubsan_handle_type_mismatch_v1(TypeMismatchData * data, uintptr_t ptr)
{
	Report(data, ptr);
}

void __ubsan_handle_type_mismatch_v1_abort(TypeMismatchData * data, uintptr_t ptr)
{
	Report(data, ptr);
}

}
