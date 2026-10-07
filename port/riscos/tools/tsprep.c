/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** tsprep: installs the Tiberian Sun data from the freeware CD images.
**
**   tsprep [-search <dir>] [-dest <dir>] [-nomovies] [-force] [<image>...]
**
** It reads the ISO 9660 images directly, so no CD drive is needed. Each image is recognised
** by its contents (the GDI disc holds INSTALL/TIBSUN.MIX, the Firestorm disc
** INSTALL/EXPAND01.MIX), so its name doesn't matter. The files the game needs are copied
** into the destination and checked against the sizes and CRC-32s of EA's freeware images.
** A file already there at the right size is skipped unless -force is given.
**
** Modelled on vcprep from the Vanilla Conquer RISC OS port.
*/

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


#define SECTOR 2048

enum { DISC_GDI = 1, DISC_FIRESTORM = 2 };

typedef struct {
	int disc;
	char const * path;		/* on the disc, any case */
	char const * name;		/* in the destination */
	uint32_t size;
	uint32_t crc;
	int movie;				/* left out by -nomovies */
	int if_missing;			/* never replaces a copy already there (another disc's is better) */
} FileType;

/* The Firestorm disc's copies of MULTI.MIX and LANGUAGE.DLL are newer than the GDI disc's. */
static FileType const Files[] = {
	{ DISC_GDI, "INSTALL/TIBSUN.MIX", "TIBSUN.MIX", 75094736u, 0x948e1a19u, 0, 0 },
	{ DISC_GDI, "MAPS01.MIX", "MAPS01.MIX", 3668236u, 0xd3e6b5d8u, 0, 0 },
	{ DISC_GDI, "SCORES.MIX", "SCORES.MIX", 51332268u, 0x669b46b9u, 0, 0 },
	{ DISC_GDI, "SIDECD01.MIX", "SIDECD01.MIX", 12304388u, 0x581c5f80u, 0, 0 },
	{ DISC_GDI, "MULTI.MIX", "MULTI.MIX", 8558956u, 0x2803c630u, 0, 1 },
	{ DISC_GDI, "MOVIES01.MIX", "MOVIES01.MIX", 489980956u, 0x190ec288u, 1, 0 },
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

static uint32_t CrcTable[256];
static unsigned char Buffer[1 << 20];
static int Problems = 0;

static void Crc_Init(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) {
			c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
		}
		CrcTable[i] = c;
	}
}

static uint32_t Crc_Update(uint32_t crc, unsigned char const * data, size_t length)
{
	while (length--) {
		crc = CrcTable[(crc ^ *data++) & 0xff] ^ (crc >> 8);
	}
	return(crc);
}

static uint32_t Read32(unsigned char const * p) { return(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

/* Reads a sector (or several) of an image. */
static int Read_Sectors(FILE * image, uint32_t lba, unsigned char * into, size_t bytes)
{
	if (fseek(image, (long)lba * SECTOR, SEEK_SET) != 0) {
		return(0);
	}
	return(fread(into, 1, bytes, image) == bytes);
}

/* Compares a directory record's name ("TIBSUN.MIX;1") with a path component, ignoring case. */
static int Same_Name(unsigned char const * record_name, int length, char const * want, size_t want_length)
{
	if (length >= 2 && record_name[length - 2] == ';') {
		length -= 2;
	}
	if (length > 0 && record_name[length - 1] == '.') {
		length--;
	}
	if ((size_t)length != want_length) {
		return(0);
	}
	for (int i = 0; i < length; i++) {
		if (toupper(record_name[i]) != toupper((unsigned char)want[i])) {
			return(0);
		}
	}
	return(1);
}

/* Finds a file by its path in an image: its first sector and size. */
static int Find_File(FILE * image, char const * path, uint32_t * lba, uint32_t * size)
{
	unsigned char pvd[SECTOR];
	if (!Read_Sectors(image, 16, pvd, SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
		return(0);
	}
	uint32_t dir_lba = Read32(pvd + 156 + 2);
	uint32_t dir_size = Read32(pvd + 156 + 10);

	char const * part = path;
	for (;;) {
		char const * slash = strchr(part, '/');
		size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
		int found = 0;

		for (uint32_t offset = 0; offset < dir_size && !found; offset += SECTOR) {
			unsigned char sector[SECTOR];
			if (!Read_Sectors(image, dir_lba + offset / SECTOR, sector, SECTOR)) {
				return(0);
			}
			for (int at = 0; at < SECTOR && sector[at] != 0;) {
				unsigned char const * record = sector + at;
				int const length = record[0];
				if (length < 34 || at + length > SECTOR) {
					break;
				}
				if (Same_Name(record + 33, record[32], part, part_length)) {
					dir_lba = Read32(record + 2);
					dir_size = Read32(record + 10);
					found = 1;
					break;
				}
				at += length;
			}
		}
		if (!found) {
			return(0);
		}
		if (slash == NULL) {
			*lba = dir_lba;
			*size = dir_size;
			return(1);
		}
		part = slash + 1;
	}
}

static int Identify(FILE * image)
{
	uint32_t lba, size;
	if (Find_File(image, "INSTALL/TIBSUN.MIX", &lba, &size)) return(DISC_GDI);
	if (Find_File(image, "INSTALL/EXPAND01.MIX", &lba, &size)) return(DISC_FIRESTORM);
	return(0);
}

static long File_Size(char const * path)
{
	struct stat info;
	return(stat(path, &info) == 0 ? (long)info.st_size : -1);
}

/* Creates the file at its full size in one go: growing a big file a little at a time is
** very slow on RISC OS's filing systems. */
static void Preallocate(char const * path, uint32_t size)
{
	FILE * f = fopen(path, "wb");
	if (f == NULL) {
		return;
	}
#ifdef __riscos__
	/* Writing the last byte first sets the extent in one go. */
	if (size > 0 && fseek(f, (long)size - 1, SEEK_SET) == 0) {
		fputc(0, f);
	}
#else
	(void)size;
#endif
	fclose(f);
}

static void Install(FILE * image, FileType const * file, int force)
{
	char const * target = file->name;

	long const existing = File_Size(target);
	if (existing >= 0 && (file->if_missing || (!force && existing == (long)file->size))) {
		printf("  %-14s already there\n", file->name);
		return;
	}

	uint32_t lba, size;
	if (!Find_File(image, file->path, &lba, &size)) {
		printf("  %-14s NOT FOUND on the disc\n", file->name);
		Problems++;
		return;
	}
	if (size != file->size) {
		printf("  %-14s is %lu bytes on this disc, not %lu as on EA's freeware disc; copying it anyway\n",
			file->name, (unsigned long)size, (unsigned long)file->size);
	}

	printf("  %-14s %3lu MB ", file->name, (unsigned long)((size + 524288) >> 20));
	fflush(stdout);

	remove(target);
	Preallocate(target, size);
	FILE * out = fopen(target, "r+b");
	if (out == NULL || fseek(image, (long)lba * SECTOR, SEEK_SET) != 0) {
		printf("can't be written: %s\n", strerror(errno));
		Problems++;
		if (out != NULL) fclose(out);
		return;
	}

	uint32_t crc = 0xffffffffu, left = size, dots = 0;
	while (left > 0) {
		size_t const chunk = left < sizeof(Buffer) ? left : sizeof(Buffer);
		if (fread(Buffer, 1, chunk, image) != chunk) {
			printf("\n    the image ended early\n");
			Problems++;
			fclose(out);
			return;
		}
		if (fwrite(Buffer, 1, chunk, out) != chunk) {
			printf("\n    writing failed: %s (is the disc full?)\n", strerror(errno));
			Problems++;
			fclose(out);
			return;
		}
		crc = Crc_Update(crc, Buffer, chunk);
		left -= (uint32_t)chunk;
		if (++dots % 16 == 0) {
			putchar('.');
			fflush(stdout);
		}
	}
	if (fclose(out) != 0) {
		printf(" writing failed: %s\n", strerror(errno));
		Problems++;
		return;
	}
	crc = ~crc;
	if (crc == file->crc) {
		printf(" OK\n");
	} else {
		printf(" copied, but its CRC is %08lx, not %08lx as on EA's freeware disc\n", (unsigned long)crc, (unsigned long)file->crc);
		Problems++;
	}
}

static int Is_Image_Name(char const * name)
{
	size_t const length = strlen(name);
	return(length > 4 && (strcasecmp(name + length - 4, ".iso") == 0 || strcasecmp(name + length - 4, "/iso") == 0));
}

/* Whether a disc's files (but its movies, and files the other disc can supply) are already in
** the current directory, at their sizes: installed by an earlier run, with that disc's image. */
static int Disc_Installed(int disc)
{
	for (size_t f = 0; f < FILE_COUNT; f++) {
		if (Files[f].disc == disc && !Files[f].movie && !Files[f].if_missing && File_Size(Files[f].name) != (long)Files[f].size) {
			return(0);
		}
	}
	return(1);
}

/* The last part of a path in either Unix or RISC OS form, for messages. */
static char const * Leaf(char const * path)
{
	char const * leaf = path;
	for (char const * p = path; *p != '\0'; p++) {
		if ((*p == '/' || *p == '.') && p[1] != '\0') {
			leaf = p + 1;
		}
	}
	return(leaf);
}

int main(int argc, char ** argv)
{
	char const * search = NULL;
	char const * dest = ".";
	int movies = 1, force = 0;
	char * images[64];
	int image_count = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-search") == 0 && i + 1 < argc) search = argv[++i];
		else if (strcmp(argv[i], "-dest") == 0 && i + 1 < argc) dest = argv[++i];
		else if (strcmp(argv[i], "-nomovies") == 0) movies = 0;
		else if (strcmp(argv[i], "-force") == 0) force = 1;
		else if (argv[i][0] == '-') { fprintf(stderr, "tsprep: unknown option %s\n", argv[i]); return(2); }
		else if (image_count < 64) images[image_count++] = argv[i];
	}

	/* The images are opened before moving to the destination, so that names given relative
	** to the current directory, or found in the search directory, still work. With no images
	** named, every big file in the search directory is tried: the images are recognised by
	** their contents, so a shortened name doesn't matter. Paths are never joined here,
	** because on RISC OS they may be in RISC OS form. */
	FILE * opened[64];
	int opened_count = 0;
	for (int i = 0; i < image_count; i++) {
		FILE * image = fopen(images[i], "rb");
		if (image == NULL) {
			printf("Can't open %s: %s\n", images[i], strerror(errno));
			Problems++;
			continue;
		}
		images[opened_count] = images[i];
		opened[opened_count++] = image;
	}
	if (image_count == 0 && search != NULL) {
		char here[1024];
		if (getcwd(here, sizeof(here)) == NULL || chdir(search) != 0) {
			printf("Can't look in %s: %s\n", search, strerror(errno));
			return(1);
		}
		DIR * dir = opendir(".");
		if (dir != NULL) {
			struct dirent * entry;
			while ((entry = readdir(dir)) != NULL && opened_count < 64) {
				if (entry->d_name[0] == '.') continue;
				if (File_Size(entry->d_name) > 1024 * 1024 || Is_Image_Name(entry->d_name)) {
					FILE * image = fopen(entry->d_name, "rb");
					if (image != NULL) {
						images[opened_count] = strdup(entry->d_name);
						opened[opened_count++] = image;
					}
				}
			}
			closedir(dir);
		}
		if (chdir(here) != 0) {
			return(1);
		}
	}
	if (chdir(dest) != 0) {
		printf("Can't write into %s: %s\n", dest, strerror(errno));
		return(1);
	}

	Crc_Init();
	{
		/* dest may be a system variable, such as <OpenTS$Dir>: name the directory itself. */
		char where[1024];
		printf("Installing the Tiberian Sun data into %s\n", getcwd(where, sizeof(where)) != NULL ? Leaf(where) : dest);
	}

	/* Firestorm first, so that its newer MULTI.MIX is the one kept. */
	int found_discs = 0;
	for (int pass = DISC_FIRESTORM; pass >= DISC_GDI; pass--) {
		for (int i = 0; i < opened_count; i++) {
			FILE * image = opened[i];
			int const disc = Identify(image);
			if (disc == pass) {
				found_discs |= disc;
				printf("\n%s disc: %s\n", disc == DISC_GDI ? "GDI" : "Firestorm", images[i]);
				for (size_t f = 0; f < FILE_COUNT; f++) {
					if (Files[f].disc == disc && (movies || !Files[f].movie)) {
						Install(image, &Files[f], force);
					}
				}
			}
		}
	}

	printf("\n");
	if (!(found_discs & DISC_GDI)) {
		if (Disc_Installed(DISC_GDI)) {
			printf("The GDI disc's files were already installed.\n");
		} else {
			printf("No GDI disc image was found. It is needed: it holds the game itself.\n");
			Problems++;
		}
	}
	if (!(found_discs & DISC_FIRESTORM)) {
		if (Disc_Installed(DISC_FIRESTORM)) {
			printf("The Firestorm disc's files were already installed.\n");
		} else {
			printf("No Firestorm disc image was found, so only Tiberian Sun itself can be played, without\n"
				"the Firestorm expansion. Add the Firestorm image and run Prepare again to install it.\n");
		}
	}
	if (Problems == 0) {
		printf("Done. Double-click !OpenTS to play.\n");
	} else {
		printf("Finished, with the problems above.\n");
	}
	return(Problems == 0 ? 0 : 1);
}
