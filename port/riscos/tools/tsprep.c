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
**   tsprep -check -dest <dir> [-old <dir>]
**   tsprep -window -images <variable> -dest <dir> [-old <dir>] [-nomovies]
**   tsprep -identify <file>
**
** It reads the ISO 9660 images directly, so no CD drive is needed. Each image is recognised
** by its contents (the GDI and Nod discs both hold INSTALL/TIBSUN.MIX, with MAPS01.MIX or
** MAPS02.MIX, the Firestorm disc INSTALL/EXPAND01.MIX), so its name doesn't matter. The files the game needs are copied
** into the destination and checked against the sizes and CRC-32s of EA's freeware images.
** A file already there at the right size is skipped unless -force is given.
**
** Each file is written under a temporary name (TIBSUN.TMP for TIBSUN.MIX) and only given its
** own once its size and CRC are right, so a file under its real name is always a whole,
** checked copy: an interrupted run (Escape, a full disc, a power cut) leaves nothing that
** looks installed, and running Prepare again redoes that file.
**
** -check only checks that the files the game needs are all there at their sizes (no
** checksums, so it takes a moment): !Run does this before every game. The first problem
** goes in OpenTS$DataProblem, which is unset when all is well; the exit code is 0 or 1.
**
** -window is how the game runs it, in a task window, when CD images are dragged onto its icon:
** the images are listed one per line in the variable -images names; the file being copied goes
** in OpenTS$Installing (its name, how far it's got in per cent, and which file of how many); afterwards the files are checked as by -check. The messages are held
** back (Say), since a task window opens at the first character printed: when all went well
** nothing is printed and the window never appears. Otherwise they are, and it waits for a key.
** -identify says which disc a file is (discid.h), in TSPrep$Disc and TSPrep$DiscCut.
** Every install (not -check) records what happened in gamedata.InstallLog (the install log,
** below), for when it goes wrong.
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
#include <time.h>
#include <unistd.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#else
#include <sys/statvfs.h>
#include <sys/time.h>
#endif

/* Which build this is, for the install log: git describe, passed in by package.sh. */
#ifndef TOOLS_BUILD
#define TOOLS_BUILD "unknown"
#endif

#include <stdarg.h>
#include "discid.h"

/* Messages: printed, or in -window mode held until it's known whether anyone needs them. */
static int Holding = 0;
static char Held[64 * 1024];
static size_t Held_Len = 0;

static int Say(char const * format, ...)
{
	va_list args;
	int n;
	va_start(args, format);
	if (Holding) {
		n = vsnprintf(Held + Held_Len, sizeof(Held) - Held_Len, format, args);
		if (n > 0) {
			Held_Len += (size_t)n < sizeof(Held) - Held_Len ? (size_t)n : sizeof(Held) - Held_Len - 1;
		}
	} else {
		n = vprintf(format, args);
	}
	va_end(args);
	return(n);
}
#define printf Say

static void Set_Var(char const * name, char const * value)
{
#ifdef __riscos__
	if (value != NULL) {
		_swix(OS_SetVarVal, _INR(0, 4), name, value, (int)strlen(value), 0, 4);
	} else {
		_swix(OS_SetVarVal, _INR(0, 4), name, NULL, -1, 0, 0);
	}
#else
	fprintf(stdout, "%s: %s\n", name, value != NULL ? value : "(unset)");
#endif
}

static char * Read_Var(char const * name)
{
#ifdef __riscos__
	static char buf[4096];
	int len = 0;
	if (_swix(OS_ReadVarVal, _INR(0, 4) | _OUT(2), name, buf, (int)sizeof(buf) - 1, 0, 0, &len) != NULL) {
		return(NULL);
	}
	buf[len] = '\0';
	return(buf);
#else
	return(getenv(name));
#endif
}

static int Window_Mode = 0;


#define SECTOR 2048

#include "tsfiles.h"

static uint32_t CrcTable[256];
static unsigned char Buffer[1 << 20];
#define CHUNK (256 * 1024)	/* the most read and written in one go */
static int Copying = 0, Copy_Count = 0;	/* for the progress: file Copying of Copy_Count */
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
	if (Find_File(image, "INSTALL/TIBSUN.MIX", &lba, &size)) {
		return(Find_File(image, "MAPS02.MIX", &lba, &size) ? DISC_NOD : DISC_GDI);
	}
	if (Find_File(image, "INSTALL/EXPAND01.MIX", &lba, &size)) return(DISC_FIRESTORM);
	return(0);
}

static long File_Size(char const * path)
{
	struct stat info;
	return(stat(path, &info) == 0 ? (long)info.st_size : -1);
}

/* Where the files go, as given (on RISC OS a RISC OS name), for the calls that need it. */
static char const * Dest_Name = ".";

/* A file in the destination, by its RISC OS name: TIBSUN.MIX is TIBSUN/MIX there. */
static void Dest_Path(char const * dir, char const * name, char * out, size_t length)
{
#ifdef __riscos__
	snprintf(out, length, "%s.%s", dir, name);
	for (char * p = out + strlen(dir) + 1; *p != '\0'; p++) {
		if (*p == '.') *p = '/';
	}
#else
	snprintf(out, length, "%s/%s", dir, name);
#endif
}

/*
** The file being written. On RISC OS it goes through the file system's own calls: OS_Args 6
** reserves its whole size at once (growing a big file a little at a time is slow on FileCore),
** without setting its length, which would fill it with zeros in one call that holds up the
** whole desktop (3.8 s for TIBSUN.MIX on a USB drive on a Pi 4). Each write is a quarter of a
** megabyte, so none of them holds up the desktop for long either.
*/
typedef struct {
	FILE * file;
	int handle;
} OutFile;

/* The last error from writing a file, as the OS put it, for the install log. */
static char Write_Error[260];

/* For the window: the error as the OS gave it (errno isn't set by the OS's own calls). */
static char const * Write_Error_Text(void)
{
	return(Write_Error[0] != '\0' ? Write_Error : strerror(errno));
}

#ifdef __riscos__
static int Note_Error(_kernel_oserror const * error)
{
	if (error != NULL) {
		snprintf(Write_Error, sizeof(Write_Error), "%s (error &%X)", error->errmess, (unsigned)error->errnum);
	}
	return(error == NULL);
}
#endif

static int Out_Open(OutFile * out, char const * name, uint32_t size)
{
	out->file = NULL;
	out->handle = 0;
#ifdef __riscos__
	char path[1024];
	Dest_Path(Dest_Name, name, path, sizeof(path));
	if (!Note_Error(_swix(OS_Find, _INR(0, 1) | _OUT(0), 0x83, path, &out->handle)) || out->handle == 0) {
		out->handle = 0;
		return(0);
	}
	_swix(OS_Args, _INR(0, 2), 6, out->handle, (int)size);
	return(1);
#else
	(void)size;
	char path[1024];
	Dest_Path(Dest_Name, name, path, sizeof(path));
	out->file = fopen(path, "wb");
	if (out->file == NULL) {
		snprintf(Write_Error, sizeof(Write_Error), "%s", strerror(errno));
	}
	return(out->file != NULL);
#endif
}

static int Out_Write(OutFile * out, void const * data, size_t length)
{
#ifdef __riscos__
	int left = 0;
	if (!Note_Error(_swix(OS_GBPB, _INR(0, 3) | _OUT(3), 2, out->handle, data, (int)length, &left))) {
		return(0);
	}
	if (left != 0) {
		snprintf(Write_Error, sizeof(Write_Error), "%d bytes weren't written", left);
	}
	return(left == 0);
#else
	if (fwrite(data, 1, length, out->file) != length) {
		snprintf(Write_Error, sizeof(Write_Error), "%s", strerror(errno));
		return(0);
	}
	return(1);
#endif
}

static int Out_Close(OutFile * out)
{
#ifdef __riscos__
	int ok = out->handle != 0 && Note_Error(_swix(OS_Find, _INR(0, 1), 0, out->handle));
	out->handle = 0;
	return(ok);
#else
	int ok = out->file != NULL && fclose(out->file) == 0;
	out->file = NULL;
	return(ok);
#endif
}

/*
** Files in the destination are always named in full: tsprep never changes the current
** directory, because on RISC OS that's the whole machine's, and while it runs in a task
** window the game (and every other program) would find theirs changed under them.
*/
static long Dest_Size(char const * name)
{
	char path[1024];
	Dest_Path(Dest_Name, name, path, sizeof(path));
#ifdef __riscos__
	int type = 0, length = 0;
	if (_swix(OS_File, _INR(0, 1) | _OUT(0) | _OUT(4), 17, path, &type, &length) != NULL || type != 1) {
		return(-1);
	}
	return((long)(unsigned)length);
#else
	return(File_Size(path));
#endif
}

static void Dest_Remove(char const * name)
{
	char path[1024];
	Dest_Path(Dest_Name, name, path, sizeof(path));
#ifdef __riscos__
	_swix(OS_File, _INR(0, 1), 6, path);
#else
	remove(path);
#endif
}

static int Dest_Rename(char const * from, char const * to)
{
	char a[1024], b[1024];
	Dest_Path(Dest_Name, from, a, sizeof(a));
	Dest_Path(Dest_Name, to, b, sizeof(b));
#ifdef __riscos__
	return(Note_Error(_swix(OS_FSControl, _INR(0, 2), 25, a, b)));
#else
	if (rename(a, b) != 0) {
		snprintf(Write_Error, sizeof(Write_Error), "%s", strerror(errno));
		return(0);
	}
	return(1);
#endif
}

static int Dest_Exists(void)
{
#ifdef __riscos__
	int type = 0;
	return(_swix(OS_File, _INR(0, 1) | _OUT(0), 17, Dest_Name, &type) == NULL && type == 2);
#else
	struct stat info;
	return(stat(Dest_Name, &info) == 0 && S_ISDIR(info.st_mode));
#endif
}

/* Every installed file is a Data file (&FFD), the INI and DLL files too. */
static void Set_Data_Type(char const * name)
{
#ifdef __riscos__
	char path[1024];
	Dest_Path(Dest_Name, name, path, sizeof(path));
	_swix(OS_File, _INR(0, 2), 18, path, 0xFFD);
#else
	(void)name;
#endif
}

/*
** ---- The install log ----
** Every install (not -check) writes what happened to gamedata.InstallLog (a Text file),
** replacing the last one, so that an install that went wrong where nobody saw the window can
** still be looked into: the machine, the images and what each was found to be, and a line per
** file with its result. Each line goes to the disc as it's written, so a crash or a power cut
** leaves what was done so far. Nothing here can make an install fail: if the log can't be
** written, it simply isn't (or it goes in the scrap directory if gamedata can't take it).
*/
static char Log_Path[1100];
static int Log_In_Scrap = 0;	/* gamedata couldn't take the log: it's in the scrap directory */
#ifdef __riscos__
static int Log_Handle = 0;
#else
static FILE * Log_File_ = NULL;
#endif

static void Log(char const * format, ...)
{
	char line[600];
	va_list args;
	va_start(args, format);
	int n = vsnprintf(line, sizeof(line) - 1, format, args);
	va_end(args);
	if (n < 0) return;
	if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
	line[n++] = '\n';
#ifdef __riscos__
	if (Log_Handle != 0) {
		_swix(OS_GBPB, _INR(0, 3), 2, Log_Handle, line, n);
		_swix(OS_Args, _INR(0, 1), 255, Log_Handle);	/* to the disc now */
	}
#else
	if (Log_File_ != NULL) {
		fwrite(line, 1, (size_t)n, Log_File_);
		fflush(Log_File_);
	}
#endif
}

/* Opens a new log at path (a Text file), replacing any there; 0 if it can't be. */
static int Log_Create(char const * path)
{
#ifdef __riscos__
	int handle = 0;
	if (_swix(OS_File, _INR(0, 5), 11, path, 0xFFF, 0, 0, 0) != NULL
		|| _swix(OS_Find, _INR(0, 1) | _OUT(0), 0xCF, path, &handle) != NULL || handle == 0) {
		return(0);
	}
	Log_Handle = handle;
#else
	if ((Log_File_ = fopen(path, "w")) == NULL) {
		return(0);
	}
#endif
	snprintf(Log_Path, sizeof(Log_Path), "%s", path);
	return(1);
}

/* Opens the log in the destination (made if need be), or else in the scrap directory. */
static void Log_Open(void)
{
	char path[1100];
#ifdef __riscos__
	_swix(OS_File, _INR(0, 1) | _IN(4), 8, Dest_Name, 0);
#else
	mkdir(Dest_Name, 0777);
#endif
	Dest_Path(Dest_Name, "InstallLog", path, sizeof(path));
	if (Log_Create(path)) {
		return;
	}
	char const * scrap = Read_Var("Wimp$ScrapDir");
#ifdef __riscos__
	if (scrap == NULL || *scrap == '\0') return;
	snprintf(path, sizeof(path), "%s.OpenTSInstallLog", scrap);
#else
	snprintf(path, sizeof(path), "%s/OpenTSInstallLog", scrap != NULL ? scrap : "/tmp");
#endif
	Log_In_Scrap = Log_Create(path);
}

static void Log_Close(void)
{
#ifdef __riscos__
	if (Log_Handle != 0) {
		_swix(OS_Find, _INR(0, 1), 0, Log_Handle);
		Log_Handle = 0;
	}
#else
	if (Log_File_ != NULL) {
		fclose(Log_File_);
		Log_File_ = NULL;
	}
#endif
}

/* Where the log is, for the window: !OpenTS.gamedata.InstallLog, or its whole path. */
static void Say_Where_Log_Is(void)
{
	if (Log_Path[0] == '\0') {
		printf("No install log could be written.\n");
		return;
	}
	if (Log_In_Scrap) {
		printf("Details are in %s (gamedata couldn't take them).\n", Log_Path);
		return;
	}
	char const * leaf = Log_Path;
	int separators = 0;
	char const sep = Log_Path[strlen(Dest_Name)];	/* '.' on RISC OS */
	for (char const * p = Log_Path + strlen(Log_Path); p > Log_Path; p--) {
		if (p[-1] == sep && ++separators == 3) {
			leaf = p;
			break;
		}
	}
	printf("Details are in %s.\n", leaf);
}

/* The time in centiseconds, for how long things took. */
static unsigned long Now_Cs(void)
{
#ifdef __riscos__
	unsigned t = 0;
	_swix(OS_ReadMonotonicTime, _OUT(0), &t);
	return(t);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return((unsigned long)tv.tv_sec * 100 + (unsigned long)tv.tv_usec / 10000);
#endif
}

/* Free space on the disc holding path, in bytes, or -1. */
static double Free_Space(char const * path)
{
#ifdef __riscos__
	unsigned lo = 0, hi = 0;
	if (_swix(OS_FSControl, _INR(0, 1) | _OUTR(0, 1), 55, path, &lo, &hi) == NULL) {	/* FreeSpace64 */
		return((double)lo + (double)hi * 4294967296.0);
	}
	if (_swix(OS_FSControl, _INR(0, 1) | _OUT(0), 49, path, &lo) == NULL) {
		return((double)lo);
	}
	return(-1);
#else
	struct statvfs info;
	return(statvfs(path, &info) == 0 ? (double)info.f_bavail * (double)info.f_frsize : -1);
#endif
}

static void Log_Free_Space(char const * when)
{
	double const free = Free_Space(Dest_Name);
	if (free >= 0) {
		Log("free space %s: %.1f MB", when, free / (1024 * 1024));
	} else {
		Log("free space %s: unknown", when);
	}
}

static void Log_Header(int argc, char ** argv)
{
	char when[40], line[1200];
	time_t const now = time(NULL);
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
	Log("tsprep install log");
	Log("build: %s, compiled %s %s", TOOLS_BUILD, __DATE__, __TIME__);
	Log("started: %s", when);
#ifdef __riscos__
	char version[200];
	_kernel_oserror const * e = _swix(OS_Byte, _INR(0, 1), 0, 0);	/* the version, as an error */
	snprintf(version, sizeof(version), "%s", e != NULL ? e->errmess : "RISC OS (version unknown)");
	for (size_t n = strlen(version); n > 0 && (unsigned char)version[n - 1] < ' '; n--) {
		version[n - 1] = '\0';
	}
	Log("system: %s", version);
	int slot = 0, next = 0, free = 0;
	if (_swix(Wimp_SlotSize, _INR(0, 1) | _OUTR(0, 2), -1, -1, &slot, &next, &free) == NULL) {
		Log("memory: wimpslot %dK, free %dK", slot / 1024, free / 1024);
	}
#else
	Log("system: not RISC OS (a test build)");
#endif
	size_t used = (size_t)snprintf(line, sizeof(line), "command:");
	for (int a = 0; a < argc && used < sizeof(line); a++) {
		used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", argv[a]);
	}
	Log("%s", line);
}

/* A file's line in the log: its name, the size it should be, what happened, and how long it took
** if it was copied: "  TIBSUN.MIX       75094736  copied, CRC ok, 31.4 s". */
static void Log_File(FileType const * file, unsigned long started, char const * format, ...)
{
	char result[400];
	va_list args;
	va_start(args, format);
	vsnprintf(result, sizeof(result), format, args);
	va_end(args);
	if (started != 0) {
		Log("  %-14s %10lu  %s, %.1f s", file->name, (unsigned long)file->size, result, (Now_Cs() - started) / 100.0);
	} else {
		Log("  %-14s %10lu  %s", file->name, (unsigned long)file->size, result);
	}
}

/*
** -check: a line at the end of the last install's log, if there is one (it isn't made, and the
** log isn't replaced): "check 2026-10-19 21:04:11: all there". !Run checks on every start, so
** the lines stop once the log reaches a few KB.
*/
static void Log_Check(char const * problem)
{
	char path[1100], line[300], when[40];
	time_t const now = time(NULL);
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
	int const n = snprintf(line, sizeof(line), "check %s: %s\n", when, problem[0] != '\0' ? problem : "all there");
	Dest_Path(Dest_Name, "InstallLog", path, sizeof(path));
#ifdef __riscos__
	int handle = 0, extent = 0;
	if (_swix(OS_Find, _INR(0, 1) | _OUT(0), 0xCF, path, &handle) != NULL || handle == 0) {
		return;
	}
	if (_swix(OS_Args, _INR(0, 1) | _OUT(2), 2, handle, &extent) == NULL && extent <= 16 * 1024) {
		_swix(OS_GBPB, _INR(0, 4), 1, handle, line, n, extent);
	}
	_swix(OS_Find, _INR(0, 1), 0, handle);
#else
	struct stat info;
	FILE * f;
	if (stat(path, &info) != 0 || info.st_size > 16 * 1024 || (f = fopen(path, "a")) == NULL) {
		return;
	}
	fwrite(line, 1, (size_t)n, f);
	fclose(f);
#endif
}

/* The temporary name a file is written under: TIBSUN.MIX becomes TIBSUN.TMP, in the same
** directory so that it can be renamed. */
static void Temp_Name(char const * name, char * out, size_t length)
{
	snprintf(out, length, "%s", name);
	char * dot = strrchr(out, '.');
	if (dot != NULL && (size_t)(dot - out) + 4 < length) {
		strcpy(dot, ".TMP");
	}
}

/* Whether Install would copy a file: not when it's already there at its size (or at all, for
** a file another disc's copy of is better). */
static int Will_Install(FileType const * file, int force)
{
	long const existing = Dest_Size(file->name);
	return(!(existing >= 0 && (file->if_missing || (!force && existing == (long)file->size))));
}

static void Install(FILE * image, FileType const * file, int force)
{
	char const * target = file->name;
	char temp[32];
	Temp_Name(file->name, temp, sizeof(temp));

	if (!Will_Install(file, force)) {
		printf("  %-14s already there\n", file->name);
		Log_File(file, 0, file->if_missing && Dest_Size(file->name) != (long)file->size
			? "already present (another disc's copy), skipped" : "already present, skipped");
		return;
	}

	uint32_t lba, size;
	if (!Find_File(image, file->path, &lba, &size)) {
		printf("  %-14s NOT FOUND on the disc\n", file->name);
		Log_File(file, 0, "NOT FOUND on the disc (%s)", file->path);
		Problems++;
		return;
	}
	if (size != file->size) {
		printf("  %-14s is %lu bytes on this disc, not %lu as on EA's freeware disc; not installed\n",
			file->name, (unsigned long)size, (unsigned long)file->size);
		Log_File(file, 0, "WRONG SIZE on the disc: %lu bytes", (unsigned long)size);
		Problems++;
		return;
	}

	printf("  %-14s %3lu MB ", file->name, (unsigned long)((size + 524288) >> 20));
	fflush(stdout);
	if (Window_Mode) {
		Set_Var("OpenTS$Installing", file->name);
	}

	Dest_Remove(temp);
	unsigned long const started = Now_Cs();
	OutFile out;
	Write_Error[0] = '\0';
	if (!Out_Open(&out, temp, size)) {
		printf("can't be written: %s\n", Write_Error_Text());
		Log_File(file, started, "CREATE ERROR, %s: %s", temp, Write_Error);
		Problems++;
		Out_Close(&out);
		Dest_Remove(temp);
		return;
	}
	if (fseek(image, (long)lba * SECTOR, SEEK_SET) != 0) {
		printf("can't be written: %s\n", strerror(errno));
		Log_File(file, started, "READ ERROR: can't seek in the image: %s", strerror(errno));
		Problems++;
		Out_Close(&out);
		Dest_Remove(temp);
		return;
	}

	Copying++;
	uint32_t crc = 0xffffffffu, left = size, dots = 0;
	int shown = -1;
	while (left > 0) {
		if (Window_Mode) {
			int const percent = (int)(((uint64_t)(size - left) * 100) / size);
			if (percent != shown) {
				char progress[80];
				snprintf(progress, sizeof(progress), "%s %d %d %d", file->name, percent, Copying, Copy_Count);
				Set_Var("OpenTS$Installing", progress);
				shown = percent;
			}
		}
		size_t const chunk = left < CHUNK ? left : CHUNK;
		if (fread(Buffer, 1, chunk, image) != chunk) {
			printf("\n    the image ended early; not installed\n");
			Log_File(file, started, "READ ERROR at byte %lu of %lu: %s", (unsigned long)(size - left), (unsigned long)size,
				ferror(image) ? strerror(errno) : "the image ends there (cut short?)");
			Problems++;
			Out_Close(&out);
			Dest_Remove(temp);
			return;
		}
		if (!Out_Write(&out, Buffer, chunk)) {
			printf("\n    writing failed: %s (is the disc full?); not installed\n", Write_Error_Text());
			Log_File(file, started, "WRITE ERROR at byte %lu: %s", (unsigned long)(size - left), Write_Error);
			Problems++;
			Out_Close(&out);
			Dest_Remove(temp);
			return;
		}
		crc = Crc_Update(crc, Buffer, chunk);
		left -= (uint32_t)chunk;
		if (++dots % 64 == 0) {
			printf(".");
			fflush(stdout);
		}
	}
	if (!Out_Close(&out)) {
		printf(" writing failed: %s; not installed\n", Write_Error_Text());
		Log_File(file, started, "WRITE ERROR on closing: %s", Write_Error);
		Problems++;
		Dest_Remove(temp);
		return;
	}
	crc = ~crc;
	if (crc != file->crc) {
		printf(" its CRC is %08lx, not %08lx as on EA's freeware disc; not installed\n", (unsigned long)crc, (unsigned long)file->crc);
		Log_File(file, started, "CRC MISMATCH: expected %08lx, got %08lx", (unsigned long)file->crc, (unsigned long)crc);
		Problems++;
		Dest_Remove(temp);
		return;
	}
	Set_Data_Type(temp);
	Dest_Remove(target);		/* RISC OS can't rename over an existing file */
	if (!Dest_Rename(temp, target)) {
		printf(" can't be given its name: %s\n", Write_Error_Text());
		Log_File(file, started, "RENAME ERROR, from %s: %s", temp, Write_Error);
		Problems++;
		Dest_Remove(temp);
		return;
	}
	printf(" OK\n");
	Log_File(file, started, "copied, CRC ok");
}

/* ---- -check: is the data all there? ---- */

static char Problem[200];

/* A file's name as RISC OS shows it (TIBSUN/MIX), for the message. */
static void Note_Problem(char const * name, char const * what)
{
	if (Problem[0] != '\0') {
		return;
	}
	snprintf(Problem, sizeof(Problem), "%s %s", name, what);
#ifdef __riscos__
	for (char * p = Problem; *p != ' '; p++) {
		if (*p == '.') *p = '/';
	}
#endif
}

static int Valid_Size(char const * name, long size)
{
	for (size_t f = 0; f < FILE_COUNT; f++) {
		if (strcmp(Files[f].name, name) == 0 && (long)Files[f].size == size) {
			return(1);
		}
	}
	return(0);
}

/* The GDI disc's files are needed (as Prepare says), at either of the sizes a file may have
** (the Firestorm disc's MULTI.MIX and SIDECD02.MIX are newer). The Nod disc and the movies
** are optional; Firestorm is too, but if any of its own files is there, all of them must be.
** A file there at a size it never has is a damaged or partly copied one: always a problem. */
static void Check_Install(void)
{
	int firestorm = 0;
	for (size_t f = 0; f < FILE_COUNT; f++) {
		long const size = Dest_Size(Files[f].name);
		if (size >= 0 && !Valid_Size(Files[f].name, size)) {
			Note_Problem(Files[f].name, "is the wrong size");
		}
		if (Files[f].disc == DISC_FIRESTORM && !Files[f].movie && size >= 0
			&& strcmp(Files[f].name, "MULTI.MIX") != 0 && strcmp(Files[f].name, "SIDECD02.MIX") != 0) {
			firestorm = 1;
		}
	}
	for (size_t f = 0; f < FILE_COUNT; f++) {
		int const needed = Files[f].disc == DISC_GDI || (firestorm && Files[f].disc == DISC_FIRESTORM);
		if (needed && !Files[f].movie && Dest_Size(Files[f].name) < 0) {
			Note_Problem(Files[f].name, "is missing");
		}
	}
}

/* Puts the problem (or nothing) in OpenTS$DataProblem, for !Run. */
static void Report_Check(void)
{
#ifdef __riscos__
	if (Problem[0] != '\0') {
		_swix(OS_SetVarVal, _INR(0, 4), "OpenTS$DataProblem", Problem, (int)strlen(Problem), 0, 4);
	} else {
		_swix(OS_SetVarVal, _INR(0, 4), "OpenTS$DataProblem", NULL, -1, 0, 0);
	}
#else
	/* Nothing goes to the screen on RISC OS: !Run runs this in the desktop. */
	printf("OpenTS$DataProblem: %s\n", Problem[0] != '\0' ? Problem : "all there");
#endif
}

/*
** -old <dir>: data installed by an earlier version, straight into !OpenTS rather than its
** gamedata directory, is moved there, file by file and only where gamedata hasn't one of its
** own: renamed, so nothing is copied and it takes no time, and if it's stopped part-way the
** next run carries on. The number moved goes in OpenTS$Moved, for the game's log.
*/
static int Moved = 0;	/* for the install log */

static void Migrate(char const * old_dir)
{
	int moved = 0;
	for (size_t f = 0; f < FILE_COUNT; f++) {
		int seen = 0;
		for (size_t g = 0; g < f; g++) {
			seen |= strcmp(Files[g].name, Files[f].name) == 0;
		}
		if (seen) continue;
		char from[1024], to[1024];
		Dest_Path(old_dir, Files[f].name, from, sizeof(from));
		Dest_Path(Dest_Name, Files[f].name, to, sizeof(to));
#ifdef __riscos__
		int from_type = 0, to_type = 0;
		if (_swix(OS_File, _INR(0, 1) | _OUT(0), 17, from, &from_type) != NULL || from_type != 1) continue;
		if (_swix(OS_File, _INR(0, 1) | _OUT(0), 17, to, &to_type) == NULL && to_type != 0) continue;
		if (moved == 0) {
			_swix(OS_File, _INR(0, 1) | _IN(4), 8, Dest_Name, 0);
		}
		if (_swix(OS_FSControl, _INR(0, 2), 25, from, to) == NULL) {
			_swix(OS_File, _INR(0, 2), 18, to, 0xFFD);
			moved++;
		}
#else
		struct stat info;
		if (stat(from, &info) != 0 || stat(to, &info) == 0) continue;
		if (moved == 0) {
			mkdir(Dest_Name, 0777);
		}
		if (rename(from, to) == 0) {
			moved++;
		}
#endif
	}
	Moved = moved;
	if (moved > 0) {
		char text[40];
		snprintf(text, sizeof(text), "%d files", moved);
		Set_Var("OpenTS$Moved", text);
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
		if (Files[f].disc == disc && !Files[f].movie && !Files[f].if_missing && Dest_Size(Files[f].name) != (long)Files[f].size) {
			return(0);
		}
	}
	return(1);
}

/* -window: shows the messages held back, says where the log is, and waits for a key, so that the
** task window stays to be read. */
static void Window_Show_Failure(void)
{
	Holding = 0;
	fwrite(Held, 1, Held_Len, stdout);
	Say_Where_Log_Is();
	printf("\nPress a key when you have read this, then close this window.\n");
	fflush(stdout);
#ifdef __riscos__
	_swix(OS_ReadC, 0);
#else
	getchar();
#endif
}

int main(int argc, char ** argv)
{
	char const * search = NULL;
	char const * dest = ".";
	int movies = 1, force = 0, check = 0;
	char const * images_var = NULL;
	char const * old_dir = NULL;
	char * images[64];
	int image_count = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-search") == 0 && i + 1 < argc) search = argv[++i];
		else if (strcmp(argv[i], "-dest") == 0 && i + 1 < argc) dest = argv[++i];
		else if (strcmp(argv[i], "-nomovies") == 0) movies = 0;
		else if (strcmp(argv[i], "-force") == 0) force = 1;
		else if (strcmp(argv[i], "-check") == 0) check = 1;
		else if (strcmp(argv[i], "-window") == 0) Window_Mode = 1;
		else if (strcmp(argv[i], "-images") == 0 && i + 1 < argc) images_var = argv[++i];
		else if (strcmp(argv[i], "-old") == 0 && i + 1 < argc) old_dir = argv[++i];
		else if (strcmp(argv[i], "-identify") == 0 && i + 1 < argc) {
			int cut;
			char volume[40];
			int d = Disc_Identify(argv[i + 1], &cut, volume, sizeof(volume));
			Set_Var("TSPrep$Disc", d >= 0 ? Disc_Types[d].id : d == DISC_NOT_ISO ? "not-iso" : "unknown");
			Set_Var("TSPrep$DiscCut", cut ? "1" : NULL);
			printf("%s: %s\n", argv[i + 1], d >= 0 ? Disc_Types[d].name : d == DISC_NOT_ISO ? "not an ISO image" : "an unknown disc");
			return(d >= 0 && !cut ? 0 : 1);
		}
		else if (argv[i][0] == '-') { fprintf(stderr, "tsprep: unknown option %s\n", argv[i]); return(2); }
		else if (image_count < 64) images[image_count++] = argv[i];
	}

	if (images_var != NULL) {
		static char list[4096];
		char const * value = Read_Var(images_var);
		if (value != NULL) {
			strncpy(list, value, sizeof(list) - 1);
			for (char * line = strtok(list, "\n"); line != NULL; line = strtok(NULL, "\n")) {
				if (*line != '\0' && image_count < 64) images[image_count++] = line;
			}
		}
	}
	if (Window_Mode) {
		Holding = 1;
	}

	Dest_Name = dest;
	if (check) {
		if (old_dir != NULL) {
			Migrate(old_dir);
		}
		if (!Dest_Exists()) {
			Note_Problem("TIBSUN.MIX", "is missing");	/* no gamedata directory: nothing installed yet */
		} else {
			Check_Install();
		}
		Report_Check();
		Log_Check(Problem);
		return(Problem[0] != '\0');
	}

	unsigned long const started = Now_Cs();
	Log_Open();
	Log_Header(argc, argv);
	for (int i = 0; i < image_count; i++) {
		Log("image given: %s", images[i]);
	}
	if (search != NULL) {
		Log("images from: %s", search);
	}
	Log("movies: %s%s", movies ? "included" : "left out (-nomovies)", force ? "; -force: files rewritten" : "");
	Log_Free_Space("before");
	if (old_dir != NULL) {
		Migrate(old_dir);
	}
	Log("moved from an earlier layout: %d file(s)", Moved);

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
			Log("image: %s: CAN'T BE OPENED: %s", images[i], strerror(errno));
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
			Log("result: FAILED: can't look in %s: %s", search, strerror(errno));
			Log_Close();
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
	/* Each image's size, what it is (discid.h) and whether it's used. */
	for (int i = 0; i < opened_count; i++) {
		int cut = 0;
		char volume[40];
		int const d = Disc_Identify(images[i], &cut, volume, sizeof(volume));
		long length = -1;
		if (fseek(opened[i], 0, SEEK_END) == 0) {
			length = ftell(opened[i]);
		}
		int const disc = Identify(opened[i]);
		Log("image: %s, %ld bytes: %s%s%s%s", images[i], length,
			d >= 0 ? Disc_Types[d].name : d == DISC_NOT_ISO ? "not a CD image (or can't be read)" : "a CD image of no disc known",
			volume[0] != '\0' ? ", volume " : "", volume, cut ? ", CUT SHORT (the download or copy didn't finish?)" : "");
		if (disc == 0) {
			Log("  not installed: not a Tiberian Sun GDI, Nod or Firestorm disc");
		}
	}
#ifdef __riscos__
	_swix(OS_File, _INR(0, 1) | _IN(4), 8, dest, 0);	/* the gamedata directory, if it isn't there yet */
#else
	mkdir(dest, 0777);
#endif
	if (!Dest_Exists()) {
		printf("Can't write into %s: it can't be made, or isn't a directory.\n", dest);
		Log("result: FAILED: can't make %s", dest);
		Log_Close();
		if (Window_Mode) {
			Window_Show_Failure();
		}
		return(1);
	}

	Crc_Init();

	/* What an interrupted run may have left under the temporary names. */
	for (size_t f = 0; f < FILE_COUNT; f++) {
		char temp[32];
		Temp_Name(Files[f].name, temp, sizeof(temp));
		Dest_Remove(temp);
	}
	{
		printf("Installing the Tiberian Sun data into %s\n", dest);
	}

	/* Firestorm first, so that its newer copies are the ones kept. */
	static int const Order[] = { DISC_FIRESTORM, DISC_GDI, DISC_NOD };
	/* How many files will be copied, for the progress ("file 2 of 6"): those not there yet,
	** each once (the GDI and Nod discs both hold TIBSUN.MIX, and the second is skipped). */
	{
		int counted = 0;
		char const * planned[FILE_COUNT];
		int planned_count = 0;
		for (size_t pass = 0; pass < sizeof(Order) / sizeof(Order[0]); pass++) {
			for (int i = 0; i < opened_count; i++) {
				int const disc = Identify(opened[i]);
				if (disc != Order[pass] || (counted & disc)) continue;
				counted |= disc;
				for (size_t f = 0; f < FILE_COUNT; f++) {
					if (Files[f].disc != disc || !(movies || !Files[f].movie) || !Will_Install(&Files[f], force)) continue;
					int dup = 0;
					for (int k = 0; k < planned_count; k++) {
						dup |= strcmp(planned[k], Files[f].name) == 0;
					}
					if (!dup) {
						planned[planned_count++] = Files[f].name;
					}
				}
			}
		}
		Copy_Count = planned_count;
	}
	int found_discs = 0;
	for (size_t pass = 0; pass < sizeof(Order) / sizeof(Order[0]); pass++) {
		for (int i = 0; i < opened_count; i++) {
			FILE * image = opened[i];
			int const disc = Identify(image);
			if (disc == Order[pass]) {
				found_discs |= disc;
				printf("\n%s disc: %s\n", disc == DISC_GDI ? "GDI" : disc == DISC_NOD ? "Nod" : "Firestorm", images[i]);
				Log("%s disc: %s, installing into %s", disc == DISC_GDI ? "GDI" : disc == DISC_NOD ? "Nod" : "Firestorm", images[i], dest);
				for (size_t f = 0; f < FILE_COUNT; f++) {
					if (Files[f].disc == disc && (movies || !Files[f].movie)) {
						Install(image, &Files[f], force);
					} else if (Files[f].disc == disc) {
						Log_File(&Files[f], 0, "left out (no movies)");
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
			printf("No GDI disc image was found. It is needed: it holds the GDI campaign and sidebar.\n");
			Log("no GDI disc given, and its files aren't installed: it is needed");
			Problems++;
		}
	}
	if (!(found_discs & DISC_NOD)) {
		if (Disc_Installed(DISC_NOD)) {
			printf("The Nod disc's files were already installed.\n");
		} else {
			printf("No Nod disc image was found, so the Nod campaign can't be played (skirmish and\n"
				"multiplayer games can still be). Add the Nod image and run Prepare again to install it.\n");
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
	if (Window_Mode) {
		Set_Var("OpenTS$Installing", NULL);
		Problem[0] = '\0';
		Check_Install();
		Report_Check();
	}
	Log_Free_Space("after");
	Log("time taken: %.1f s", (Now_Cs() - started) / 100.0);
	if (Problems != 0 || Problem[0] != '\0') {
		Log("result: FAILED: %d problem(s)%s%s", Problems, Problem[0] != '\0' ? "; the game's data is still incomplete: " : "", Problem);
	} else {
		Log("result: OK");
	}
	Log_Close();
	if (Window_Mode) {
		if (Problems == 0 && Problem[0] == '\0') {
			return(0); /* nothing printed: the task window never opens */
		}
		if (Problem[0] != '\0') {
			printf("\nThe game's data is still incomplete: %s.\n", Problem);
		}
		Window_Show_Failure();
		return(1);
	}
	if (Problems == 0) {
		printf("Done. Double-click !OpenTS to play.\n");
	} else {
		printf("Finished, with the problems above.\n");
		Say_Where_Log_Is();
	}
	return(Problems == 0 ? 0 : 1);
}
