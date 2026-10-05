/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The debug log for the POSIX build, in place of code/dbgprint.cpp. Every message goes to
** stderr and to Debug/DEBUG_<date>_<time>.LOG beside the program; there is no console window.
*/

#include "dbgprint.h"
#include "../compat/windows.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fnmatch.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static constexpr size_t DEBUG_MESSAGE_MAX = 4096;
static constexpr unsigned DEBUG_LOG_MAX_AGE_DAYS = 14;

// Made on first use: other files' static objects log before this file's are constructed.
static std::recursive_mutex & Debug_Lock(void)
{
	static std::recursive_mutex lock;
	return(lock);
}
static bool DebugInitDone = false;
static bool AtLineStart = true;
static FILE * DebugFile = nullptr;
static char DebugDirectory[MAX_PATH];
static char DebugFileName[MAX_PATH];

bool Delete_Files_Older_Than(char const * directory, char const * pattern, unsigned days)
{
	DIR * dir = opendir(directory);
	if (dir == nullptr) {
		return(false);
	}
	time_t const cutoff = time(nullptr) - time_t(days) * 24 * 60 * 60;
	while (dirent * entry = readdir(dir)) {
		if (fnmatch(pattern, entry->d_name, FNM_CASEFOLD) != 0) {
			continue;
		}
		std::string path = std::string(directory) + "/" + entry->d_name;
		struct stat info;
		if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_mtime < cutoff) {
			unlink(path.c_str());
		}
	}
	closedir(dir);
	return(true);
}

static void Init_Locked(void)
{
	if (DebugInitDone) {
		return;
	}
	DebugInitDone = true;

	char path_to_exe[MAX_PATH];
	if (GetModuleFileName(nullptr, path_to_exe, sizeof(path_to_exe)) != 0) {
		char * slash = strrchr(path_to_exe, '/');
		if (slash != nullptr) {
			slash[1] = '\0';
		} else {
			path_to_exe[0] = '\0';
		}
		snprintf(DebugDirectory, sizeof(DebugDirectory), "%sDebug", path_to_exe);
	}

	time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	char timestamp[32];
	strftime(timestamp, sizeof(timestamp), "%d-%m-%Y_%H-%M-%S", &local);

	if (DebugDirectory[0] != '\0' && (mkdir(DebugDirectory, 0777) == 0 || errno == EEXIST)) {
		Delete_Files_Older_Than(DebugDirectory, "DEBUG_*.LOG", DEBUG_LOG_MAX_AGE_DAYS);
		snprintf(DebugFileName, sizeof(DebugFileName), "%s/DEBUG_%s_%d.LOG", DebugDirectory, timestamp, int(getpid()));
		DebugFile = fopen(DebugFileName, "w");
		if (DebugFile == nullptr) {
			DebugFileName[0] = '\0';
		}
	}
}

static void Write_Text_Locked(char const * text, size_t length)
{
	fwrite(text, 1, length, stderr);
	if (DebugFile != nullptr) {
		fwrite(text, 1, length, DebugFile);
		fflush(DebugFile);
	}
}

static void Emit(char const * buffer, bool with_prefix)
{
	std::lock_guard<std::recursive_mutex> lock(Debug_Lock());
	Init_Locked();

	char const * text = buffer;
	while (*text != '\0') {
		if (AtLineStart && with_prefix) {
			char stamp[32];
			int n = snprintf(stamp, sizeof(stamp), "[%u] ", unsigned(GetTickCount()));
			Write_Text_Locked(stamp, size_t(n));
		}
		char const * newline = strchr(text, '\n');
		size_t length = newline != nullptr ? size_t(newline - text) + 1 : strlen(text);
		Write_Text_Locked(text, length);
		AtLineStart = newline != nullptr;
		text += length;
	}
	fflush(stderr);
}

void Debug_Init(void)
{
	std::lock_guard<std::recursive_mutex> lock(Debug_Lock());
	Init_Locked();
}

void Debug_Init_Console(void)
{
	Debug_Init();
}

void Debug_Console_Hold(void)
{
}

char const * Debug_Log_File_Name(void)
{
	Debug_Init();
	return(DebugFileName);
}

char const * Debug_Directory(void)
{
	Debug_Init();
	return(DebugDirectory);
}

void __cdecl DebugString(char const * string, ...)
{
	int const last_errno = errno;
	char buffer[DEBUG_MESSAGE_MAX];
	va_list va;
	va_start(va, string);
	vsnprintf(buffer, sizeof(buffer), string, va);
	va_end(va);
	Emit(buffer, true);
	errno = last_errno;
}

void __cdecl DebugStringNoPrefix(char const * string, ...)
{
	int const last_errno = errno;
	char buffer[DEBUG_MESSAGE_MAX];
	va_list va;
	va_start(va, string);
	vsnprintf(buffer, sizeof(buffer), string, va);
	va_end(va);
	Emit(buffer, false);
	errno = last_errno;
}

char const * Last_Error_Text(unsigned long error)
{
	static thread_local char message_buffer[256];
	snprintf(message_buffer, sizeof(message_buffer), "%s", strerror(int(error)));
	return(message_buffer);
}
