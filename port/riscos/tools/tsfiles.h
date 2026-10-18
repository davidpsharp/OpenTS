/*
** tsfiles.h: the files the Tiberian Sun game needs from each freeware CD image, with their
** sizes and CRC-32s. Shared by Utils.tsprep, which installs and checks them, and the game's
** desktop code (port/riscos/desktop.cpp), which works out from it what a dropped disc would
** add and which discs are installed. All of them go in the gamedata directory inside
** !OpenTS, under these names (RISC OS shows TIBSUN.MIX as TIBSUN/MIX).
**
** C, header-only, so it can be included by C and C++ alike.
*/
#ifndef TSFILES_H
#define TSFILES_H

#include <stdint.h>

enum { DISC_GDI = 1, DISC_FIRESTORM = 2, DISC_NOD = 4 };

typedef struct {
	int disc;
	char const * path;		/* on the disc, any case */
	char const * name;		/* in the destination */
	uint32_t size;
	uint32_t crc;
	int movie;				/* left out by -nomovies */
	int if_missing;			/* never replaces a copy already there (another disc's is better) */
} FileType;

/* The Firestorm disc's copies of MULTI.MIX, LANGUAGE.DLL and SIDECD02.MIX are newer than the
** GDI and Nod discs'. The Nod disc holds the GDI disc's TIBSUN.MIX, SCORES.MIX and MULTI.MIX
** too, and its own campaign (MAPS02.MIX) and movies. */
static FileType const Files[] = {
	{ DISC_GDI, "INSTALL/TIBSUN.MIX", "TIBSUN.MIX", 75094736u, 0x948e1a19u, 0, 0 },
	{ DISC_GDI, "MAPS01.MIX", "MAPS01.MIX", 3668236u, 0xd3e6b5d8u, 0, 0 },
	{ DISC_GDI, "SCORES.MIX", "SCORES.MIX", 51332268u, 0x669b46b9u, 0, 0 },
	{ DISC_GDI, "SIDECD01.MIX", "SIDECD01.MIX", 12304388u, 0x581c5f80u, 0, 0 },
	{ DISC_GDI, "MULTI.MIX", "MULTI.MIX", 8558956u, 0x2803c630u, 0, 1 },
	{ DISC_GDI, "MOVIES01.MIX", "MOVIES01.MIX", 489980956u, 0x190ec288u, 1, 0 },
	{ DISC_NOD, "INSTALL/TIBSUN.MIX", "TIBSUN.MIX", 75094736u, 0x948e1a19u, 0, 0 },
	{ DISC_NOD, "MAPS02.MIX", "MAPS02.MIX", 3503740u, 0xdddd67a8u, 0, 0 },
	{ DISC_NOD, "SCORES.MIX", "SCORES.MIX", 51332268u, 0x669b46b9u, 0, 0 },
	{ DISC_NOD, "SIDECD02.MIX", "SIDECD02.MIX", 11141812u, 0xe9a00880u, 0, 1 },
	{ DISC_NOD, "MULTI.MIX", "MULTI.MIX", 8558956u, 0x2803c630u, 0, 1 },
	{ DISC_NOD, "MOVIES02.MIX", "MOVIES02.MIX", 490656380u, 0xf2ea4a2au, 1, 0 },
	{ DISC_FIRESTORM, "INSTALL/EXPAND01.MIX", "EXPAND01.MIX", 36972996u, 0xa2549943u, 0, 0 },
	{ DISC_FIRESTORM, "INSTALL/LANGUAGE.DLL", "LANGUAGE.DLL", 122880u, 0x98688c5eu, 0, 0 },
	{ DISC_FIRESTORM, "INSTALL/MULTI.MIX", "MULTI.MIX", 11960308u, 0xc462174eu, 0, 0 },
	{ DISC_FIRESTORM, "INSTALL/WDTVOX.MIX", "WDTVOX.MIX", 2800932u, 0xb02232acu, 0, 0 },
	{ DISC_FIRESTORM, "E01SCD01.MIX", "E01SCD01.MIX", 350116u, 0x93221a0fu, 0, 0 },
	{ DISC_FIRESTORM, "E01SCD02.MIX", "E01SCD02.MIX", 275012u, 0xe8fbc627u, 0, 0 },
	{ DISC_FIRESTORM, "MAPS03.MIX", "MAPS03.MIX", 3714132u, 0x6cc786a6u, 0, 0 },
	{ DISC_FIRESTORM, "SCORES01.MIX", "SCORES01.MIX", 20079580u, 0xbee0f68bu, 0, 0 },
	{ DISC_FIRESTORM, "SIDECD02.MIX", "SIDECD02.MIX", 11141636u, 0xff089862u, 0, 0 },
	{ DISC_FIRESTORM, "WDT.MIX", "WDT.MIX", 11806764u, 0x9bb4c6e7u, 0, 0 },
	{ DISC_FIRESTORM, "MOVIES03.MIX", "MOVIES03.MIX", 462369668u, 0xd1fdaaabu, 1, 0 },
};
#define FILE_COUNT (sizeof(Files) / sizeof(Files[0]))

/* The discs' names, for messages. */
static inline char const * Ts_Disc_Name(int disc)
{
	return(disc == DISC_GDI ? "GDI" : disc == DISC_NOD ? "Nod" : disc == DISC_FIRESTORM ? "Firestorm" : "?");
}

#endif
