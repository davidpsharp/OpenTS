/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The Win32 functions the engine calls, for the POSIX build (windows.h lists them). Paths
** may use either slash. GDI, registry and window-message calls report failure or do nothing,
** because the SDL code and the software renderer replace them.
*/

#include "windows.h"
#include "io.h"
#include "rcstrings.h"
#include "../posix/stackprime.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#ifndef __riscos__
#include <sys/statvfs.h>
#endif
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static thread_local DWORD _LastError = 0;

static DWORD Error_From_Errno(int error)
{
	switch (error) {
		case 0:			return(ERROR_SUCCESS);
		case ENOENT:	return(ERROR_FILE_NOT_FOUND);
		case ENOTDIR:	return(ERROR_PATH_NOT_FOUND);
		case EEXIST:	return(ERROR_ALREADY_EXISTS);
		default:		return(0x1000 + (DWORD)error);
	}
}

static void Set_Errno_Error(void)
{
	_LastError = Error_From_Errno(errno);
}

DWORD GetLastError(void) { return(_LastError); }
void SetLastError(DWORD error) { _LastError = error; }

static std::string Native_Path(char const * name)
{
	std::string path = (name != nullptr) ? name : "";
	std::replace(path.begin(), path.end(), '\\', '/');
	return(path);
}

/*
** Strings
*/
char * ultoa(unsigned long value, char * buffer, int radix)
{
	char digits[sizeof(unsigned long) * 8 + 1];
	int count = 0;
	do {
		unsigned d = (unsigned)(value % (unsigned long)radix);
		digits[count++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		value /= (unsigned long)radix;
	} while (value != 0);
	for (int index = 0; index < count; index++) {
		buffer[index] = digits[count - 1 - index];
	}
	buffer[count] = '\0';
	return(buffer);
}

char * ltoa(long value, char * buffer, int radix)
{
	if (radix == 10 && value < 0) {
		buffer[0] = '-';
		ultoa((unsigned long)(-(value + 1)) + 1, buffer + 1, radix);
		return(buffer);
	}
	return(ultoa((unsigned long)value, buffer, radix));
}

char * itoa(int value, char * buffer, int radix) { return(ltoa(value, buffer, radix)); }

/*
** Time
*/
typedef std::chrono::steady_clock SteadyClock;
static SteadyClock::time_point const _Start = SteadyClock::now();

static std::uint64_t Microseconds(void)
{
	return((std::uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - _Start).count());
}

DWORD timeGetTime(void) { return((DWORD)(Microseconds() / 1000)); }
DWORD GetTickCount(void) { return(timeGetTime()); }
ULONGLONG GetTickCount64(void) { return(Microseconds() / 1000); }

BOOL QueryPerformanceCounter(LARGE_INTEGER * count)
{
	count->QuadPart = (LONGLONG)Microseconds();
	return(TRUE);
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency)
{
	frequency->QuadPart = 1000000;
	return(TRUE);
}

void Sleep(DWORD milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

UINT timeBeginPeriod(UINT) { return(TIMERR_NOERROR); }
UINT timeEndPeriod(UINT) { return(TIMERR_NOERROR); }

// FILETIME counts 100 ns intervals from 1601; time_t counts seconds from 1970.
static std::uint64_t const FILETIME_UNIX_EPOCH = 116444736000000000ULL;

static FILETIME FileTime_From_Unix(time_t seconds, long nanoseconds)
{
	std::uint64_t value = FILETIME_UNIX_EPOCH + (std::uint64_t)seconds * 10000000ULL + (std::uint64_t)(nanoseconds / 100);
	FILETIME result;
	result.dwLowDateTime = (DWORD)value;
	result.dwHighDateTime = (DWORD)(value >> 32);
	return(result);
}

static std::uint64_t FileTime_Value(FILETIME const * time)
{
	return(((std::uint64_t)time->dwHighDateTime << 32) | time->dwLowDateTime);
}

static void System_Time_From_Tm(struct tm const & t, unsigned milliseconds, SYSTEMTIME * time)
{
	time->wYear = (WORD)(t.tm_year + 1900);
	time->wMonth = (WORD)(t.tm_mon + 1);
	time->wDayOfWeek = (WORD)t.tm_wday;
	time->wDay = (WORD)t.tm_mday;
	time->wHour = (WORD)t.tm_hour;
	time->wMinute = (WORD)t.tm_min;
	time->wSecond = (WORD)t.tm_sec;
	time->wMilliseconds = (WORD)milliseconds;
}

static void Now(time_t & seconds, unsigned & milliseconds)
{
	auto now = std::chrono::system_clock::now();
	seconds = std::chrono::system_clock::to_time_t(now);
	milliseconds = (unsigned)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
}

void GetLocalTime(SYSTEMTIME * time)
{
	time_t seconds;
	unsigned ms;
	Now(seconds, ms);
	struct tm t;
	localtime_r(&seconds, &t);
	System_Time_From_Tm(t, ms, time);
}

void GetSystemTime(SYSTEMTIME * time)
{
	time_t seconds;
	unsigned ms;
	Now(seconds, ms);
	struct tm t;
	gmtime_r(&seconds, &t);
	System_Time_From_Tm(t, ms, time);
}

void GetSystemTimeAsFileTime(FILETIME * time)
{
	auto now = std::chrono::system_clock::now();
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	*time = FileTime_From_Unix((time_t)(ns / 1000000000), (long)(ns % 1000000000));
}

BOOL FileTimeToSystemTime(FILETIME const * file, SYSTEMTIME * system)
{
	std::uint64_t value = FileTime_Value(file);
	if (value < FILETIME_UNIX_EPOCH) {
		value = FILETIME_UNIX_EPOCH;
	}
	time_t seconds = (time_t)((value - FILETIME_UNIX_EPOCH) / 10000000ULL);
	unsigned ms = (unsigned)(((value - FILETIME_UNIX_EPOCH) / 10000ULL) % 1000);
	struct tm t;
	gmtime_r(&seconds, &t);
	System_Time_From_Tm(t, ms, system);
	return(TRUE);
}

BOOL FileTimeToLocalFileTime(FILETIME const * file, FILETIME * local)
{
	std::uint64_t value = FileTime_Value(file);
	time_t seconds = (time_t)((value - FILETIME_UNIX_EPOCH) / 10000000ULL);
	struct tm t;
	localtime_r(&seconds, &t);
	std::int64_t offset = (std::int64_t)t.tm_gmtoff * 10000000LL;
	value = (std::uint64_t)((std::int64_t)value + offset);
	local->dwLowDateTime = (DWORD)value;
	local->dwHighDateTime = (DWORD)(value >> 32);
	return(TRUE);
}

BOOL SystemTimeToFileTime(SYSTEMTIME const * system, FILETIME * file)
{
	struct tm t = {};
	t.tm_year = system->wYear - 1900;
	t.tm_mon = system->wMonth - 1;
	t.tm_mday = system->wDay;
	t.tm_hour = system->wHour;
	t.tm_min = system->wMinute;
	t.tm_sec = system->wSecond;
	time_t seconds = timegm(&t);
	*file = FileTime_From_Unix(seconds, (long)system->wMilliseconds * 1000000L);
	return(TRUE);
}

LONG CompareFileTime(FILETIME const * a, FILETIME const * b)
{
	std::uint64_t x = FileTime_Value(a);
	std::uint64_t y = FileTime_Value(b);
	return(x < y ? -1 : (x > y ? 1 : 0));
}

int GetDateFormat(DWORD, DWORD, SYSTEMTIME const * time, char const *, char * buffer, int size)
{
	return(snprintf(buffer, (size_t)size, "%02u/%02u/%04u", time->wDay, time->wMonth, time->wYear) + 1);
}

int GetTimeFormat(DWORD, DWORD flags, SYSTEMTIME const * time, char const *, char * buffer, int size)
{
	if (flags & (TIME_NOSECONDS | TIME_NOMINUTESORSECONDS)) {
		return(snprintf(buffer, (size_t)size, "%02u:%02u", time->wHour, time->wMinute) + 1);
	}
	return(snprintf(buffer, (size_t)size, "%02u:%02u:%02u", time->wHour, time->wMinute, time->wSecond) + 1);
}

/*
** Handles: files, and events, which also stand in for mutexes and threads.
*/
struct FileHandle
{
	int Descriptor;
};

struct EventHandle
{
	std::mutex Mutex;
	std::condition_variable Condition;
	bool Signalled;
	bool ManualReset;
};

enum HandleKind { HANDLE_FILE, HANDLE_EVENT };

// Every handle starts with its kind, so CloseHandle knows what it frees.
struct HandleHeader
{
	HandleKind Kind;
};

struct FileHandleBox
{
	HandleHeader Header;
	FileHandle File;
};

struct EventHandleBox
{
	HandleHeader Header;
	EventHandle Event;
};

static HANDLE Wrap_File(int fd)
{
	FileHandleBox * box = new FileHandleBox;
	box->Header.Kind = HANDLE_FILE;
	box->File.Descriptor = fd;
	return((HANDLE)box);
}

/*
** Files
*/

HANDLE CreateFile(char const * name, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD creation, DWORD, HANDLE)
{
	std::string path = Native_Path(name);
	int flags = 0;
	if ((access & GENERIC_READ) && (access & GENERIC_WRITE)) {
		flags = O_RDWR;
	} else if (access & GENERIC_WRITE) {
		flags = O_WRONLY;
	} else {
		flags = O_RDONLY;
	}
	switch (creation) {
		case CREATE_NEW:		flags |= O_CREAT | O_EXCL; break;
		case CREATE_ALWAYS:		flags |= O_CREAT | O_TRUNC; break;
		case OPEN_ALWAYS:		flags |= O_CREAT; break;
		case TRUNCATE_EXISTING:	flags |= O_TRUNC; break;
		default:				break;
	}
	int fd = open(path.c_str(), flags, 0666);
	if (fd < 0) {
		Set_Errno_Error();
		return(INVALID_HANDLE_VALUE);
	}
	_LastError = ERROR_SUCCESS;
	return(Wrap_File(fd));
}

static int Descriptor(HANDLE file)
{
	if (file == nullptr || file == INVALID_HANDLE_VALUE) {
		return(-1);
	}
	return(((FileHandleBox *)file)->File.Descriptor);
}

BOOL ReadFile(HANDLE file, void * buffer, DWORD size, DWORD * read, void *)
{
	ssize_t result = ::read(Descriptor(file), buffer, size);
	if (result < 0) {
		Set_Errno_Error();
		if (read != nullptr) *read = 0;
		return(FALSE);
	}
	if (read != nullptr) *read = (DWORD)result;
	return(TRUE);
}

BOOL WriteFile(HANDLE file, void const * buffer, DWORD size, DWORD * written, void *)
{
	ssize_t result = ::write(Descriptor(file), buffer, size);
	if (result < 0) {
		Set_Errno_Error();
		if (written != nullptr) *written = 0;
		return(FALSE);
	}
	if (written != nullptr) *written = (DWORD)result;
	return(TRUE);
}

BOOL FlushFileBuffers(HANDLE file)
{
	return(fsync(Descriptor(file)) == 0);
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG * high, DWORD method)
{
	off_t offset = (off_t)distance;
	if (high != nullptr) {
		offset = (off_t)(((std::int64_t)*high << 32) | (std::uint32_t)distance);
	}
	int whence = (method == FILE_CURRENT) ? SEEK_CUR : (method == FILE_END) ? SEEK_END : SEEK_SET;
	off_t result = lseek(Descriptor(file), offset, whence);
	if (result < 0) {
		Set_Errno_Error();
		return(INVALID_SET_FILE_POINTER);
	}
	if (high != nullptr) {
		*high = (LONG)((std::int64_t)result >> 32);
	}
	return((DWORD)result);
}

DWORD GetFileSize(HANDLE file, DWORD * high)
{
	struct stat info;
	if (fstat(Descriptor(file), &info) != 0) {
		Set_Errno_Error();
		return(INVALID_FILE_SIZE);
	}
	if (high != nullptr) {
		*high = (DWORD)((std::uint64_t)info.st_size >> 32);
	}
	return((DWORD)info.st_size);
}

long filelength(int handle)
{
	struct stat info;
	if (fstat(handle, &info) != 0) {
		return(-1);
	}
	return((long)info.st_size);
}

static FILETIME Modified_Time(struct stat const & info)
{
#ifdef __APPLE__
	return(FileTime_From_Unix(info.st_mtimespec.tv_sec, info.st_mtimespec.tv_nsec));
#else
	return(FileTime_From_Unix(info.st_mtime, 0));
#endif
}

BOOL GetFileTime(HANDLE file, FILETIME * creation, FILETIME * access, FILETIME * write)
{
	struct stat info;
	if (fstat(Descriptor(file), &info) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	FILETIME modified = Modified_Time(info);
	if (creation != nullptr) *creation = modified;
	if (access != nullptr) *access = modified;
	if (write != nullptr) *write = modified;
	return(TRUE);
}

static DWORD Attributes_Of(struct stat const & info, char const * leaf)
{
	DWORD attributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	if (leaf != nullptr && leaf[0] == '.') {
		attributes |= FILE_ATTRIBUTE_HIDDEN;
	}
	return(attributes);
}

DWORD GetFileAttributes(char const * name)
{
	std::string path = Native_Path(name);
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		Set_Errno_Error();
		return(INVALID_FILE_ATTRIBUTES);
	}
	return(Attributes_Of(info, nullptr));
}

BOOL GetFileAttributesEx(char const * name, GET_FILEEX_INFO_LEVELS, void * result)
{
	std::string path = Native_Path(name);
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	WIN32_FILE_ATTRIBUTE_DATA * data = (WIN32_FILE_ATTRIBUTE_DATA *)result;
	data->dwFileAttributes = Attributes_Of(info, nullptr);
	data->ftCreationTime = data->ftLastAccessTime = data->ftLastWriteTime = Modified_Time(info);
	data->nFileSizeHigh = (DWORD)((std::uint64_t)info.st_size >> 32);
	data->nFileSizeLow = (DWORD)info.st_size;
	return(TRUE);
}

BOOL CreateDirectory(char const * name, void *)
{
	std::string path = Native_Path(name);
	if (mkdir(path.c_str(), 0777) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	return(TRUE);
}

BOOL RemoveDirectory(char const * name)
{
	std::string path = Native_Path(name);
	return(rmdir(path.c_str()) == 0);
}

BOOL DeleteFile(char const * name)
{
	std::string path = Native_Path(name);
	if (unlink(path.c_str()) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	return(TRUE);
}

BOOL MoveFileEx(char const * from, char const * to, DWORD flags)
{
	std::string source = Native_Path(from);
	std::string dest = Native_Path(to);
	if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(dest.c_str(), F_OK) == 0) {
		_LastError = ERROR_ALREADY_EXISTS;
		return(FALSE);
	}
	if (rename(source.c_str(), dest.c_str()) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	return(TRUE);
}

BOOL MoveFile(char const * from, char const * to)
{
	return(MoveFileEx(from, to, 0));
}

BOOL CopyFile(char const * from, char const * to, BOOL failifexists)
{
	std::string source = Native_Path(from);
	std::string dest = Native_Path(to);
	if (failifexists && access(dest.c_str(), F_OK) == 0) {
		_LastError = ERROR_ALREADY_EXISTS;
		return(FALSE);
	}
	FILE * in = fopen(source.c_str(), "rb");
	if (in == nullptr) {
		Set_Errno_Error();
		return(FALSE);
	}
	FILE * out = fopen(dest.c_str(), "wb");
	if (out == nullptr) {
		Set_Errno_Error();
		fclose(in);
		return(FALSE);
	}
	char buffer[65536];
	size_t count;
	bool ok = true;
	while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, count, out) != count) {
			ok = false;
			break;
		}
	}
	fclose(in);
	ok = (fclose(out) == 0) && ok;
	return(ok);
}

DWORD GetCurrentDirectory(DWORD size, char * buffer)
{
	if (getcwd(buffer, size) == nullptr) {
		Set_Errno_Error();
		return(0);
	}
	return((DWORD)strlen(buffer));
}

BOOL SetCurrentDirectory(char const * name)
{
	std::string path = Native_Path(name);
	return(chdir(path.c_str()) == 0);
}

DWORD GetModuleFileName(HMODULE, char * buffer, DWORD size)
{
	if (size == 0) {
		return(0);
	}
	buffer[0] = '\0';
#if defined(__APPLE__)
	char path[4096];
	uint32_t length = sizeof(path);
	if (_NSGetExecutablePath(path, &length) != 0) {
		return(0);
	}
	char resolved[4096];
	if (realpath(path, resolved) == nullptr) {
		snprintf(resolved, sizeof(resolved), "%s", path);
	}
	snprintf(buffer, size, "%s", resolved);
#elif defined(__linux__)
	ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
	if (length < 0) {
		return(0);
	}
	buffer[length] = '\0';
#else
	// RISC OS: !Run starts the program in the application directory.
	char directory[1024];
	if (getcwd(directory, sizeof(directory)) == nullptr) {
		return(0);
	}
	snprintf(buffer, size, "%s/OpenTS", directory);
#endif
	return((DWORD)strlen(buffer));
}

DWORD GetFullPathName(char const * name, DWORD size, char * buffer, char ** filepart)
{
	std::string path = Native_Path(name);
	if (!path.empty() && path[0] != '/') {
		char directory[1024];
		if (getcwd(directory, sizeof(directory)) != nullptr) {
			path = std::string(directory) + "/" + path;
		}
	}
	snprintf(buffer, size, "%s", path.c_str());
	if (filepart != nullptr) {
		char * slash = strrchr(buffer, '/');
		*filepart = (slash != nullptr) ? slash + 1 : buffer;
	}
	return((DWORD)strlen(buffer));
}

BOOL GetDiskFreeSpaceEx(char const * directory, ULARGE_INTEGER * available, ULARGE_INTEGER * total, ULARGE_INTEGER * free)
{
#ifdef __riscos__
	// UnixLib has no statvfs; report plenty, as the game only checks there is some.
	(void)directory;
	std::uint64_t const plenty = 1024ULL * 1024 * 1024;
	if (available != nullptr) available->QuadPart = plenty;
	if (total != nullptr) total->QuadPart = plenty;
	if (free != nullptr) free->QuadPart = plenty;
	return(TRUE);
#else
	std::string path = Native_Path((directory != nullptr && directory[0] != '\0') ? directory : ".");
	struct statvfs info;
	if (statvfs(path.c_str(), &info) != 0) {
		Set_Errno_Error();
		return(FALSE);
	}
	std::uint64_t block = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
	if (available != nullptr) available->QuadPart = block * info.f_bavail;
	if (total != nullptr) total->QuadPart = block * info.f_blocks;
	if (free != nullptr) free->QuadPart = block * info.f_bfree;
	return(TRUE);
#endif
}

/*
** FindFirstFile and FindNextFile: a directory listing filtered by a DOS wildcard, matched
** without regard to case.
*/
struct FindHandle
{
	DIR * Directory;
	std::string Path;
	std::string Pattern;
};

static bool Wildcard_Match(char const * pattern, char const * name)
{
	if (strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0) {
		return(true);
	}
	while (*pattern != '\0') {
		if (*pattern == '*') {
			pattern++;
			if (*pattern == '\0') return(true);
			for (; *name != '\0'; name++) {
				if (Wildcard_Match(pattern, name)) return(true);
			}
			return(Wildcard_Match(pattern, name));
		}
		if (*name == '\0') {
			// "name.*" matches "name" as it does on Windows.
			return(strcmp(pattern, ".*") == 0);
		}
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name)) {
			return(false);
		}
		pattern++;
		name++;
	}
	return(*name == '\0');
}

static bool Find_Next(FindHandle * find, WIN32_FIND_DATA * data)
{
	while (dirent * entry = readdir(find->Directory)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!Wildcard_Match(find->Pattern.c_str(), entry->d_name)) {
			continue;
		}
		std::string full = find->Path + entry->d_name;
		struct stat info;
		if (stat(full.c_str(), &info) != 0) {
			continue;
		}
		memset(data, 0, sizeof(*data));
		data->dwFileAttributes = Attributes_Of(info, entry->d_name);
		data->ftCreationTime = data->ftLastAccessTime = data->ftLastWriteTime = Modified_Time(info);
		data->nFileSizeHigh = (DWORD)((std::uint64_t)info.st_size >> 32);
		data->nFileSizeLow = (DWORD)info.st_size;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", entry->d_name);
		snprintf(data->cAlternateFileName, sizeof(data->cAlternateFileName), "%s", entry->d_name);
		return(true);
	}
	_LastError = ERROR_NO_MORE_FILES;
	return(false);
}

HANDLE FindFirstFile(char const * pattern, WIN32_FIND_DATA * data)
{
	std::string path = Native_Path(pattern);
	std::string directory = ".";
	std::string leaf = path;
	size_t slash = path.rfind('/');
	if (slash != std::string::npos) {
		directory = path.substr(0, slash);
		leaf = path.substr(slash + 1);
		if (directory.empty()) directory = "/";
	}
	DIR * dir = opendir(directory.c_str());
	if (dir == nullptr) {
		Set_Errno_Error();
		return(INVALID_HANDLE_VALUE);
	}
	FindHandle * find = new FindHandle;
	find->Directory = dir;
	find->Path = (directory == "/") ? "/" : directory + "/";
	find->Pattern = leaf;
	if (!Find_Next(find, data)) {
		closedir(dir);
		delete find;
		_LastError = ERROR_FILE_NOT_FOUND;
		return(INVALID_HANDLE_VALUE);
	}
	return((HANDLE)find);
}

BOOL FindNextFile(HANDLE handle, WIN32_FIND_DATA * data)
{
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
		return(FALSE);
	}
	return(Find_Next((FindHandle *)handle, data));
}

BOOL FindClose(HANDLE handle)
{
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
		return(FALSE);
	}
	FindHandle * find = (FindHandle *)handle;
	closedir(find->Directory);
	delete find;
	return(TRUE);
}

BOOL CloseHandle(HANDLE handle)
{
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
		return(FALSE);
	}
	HandleHeader * header = (HandleHeader *)handle;
	if (header->Kind == HANDLE_FILE) {
		FileHandleBox * box = (FileHandleBox *)handle;
		int result = close(box->File.Descriptor);
		delete box;
		return(result == 0);
	}
	delete (EventHandleBox *)handle;
	return(TRUE);
}

static EventHandle * Event_Of(HANDLE handle)
{
	return(&((EventHandleBox *)handle)->Event);
}

HANDLE CreateEvent(LPSECURITY_ATTRIBUTES, BOOL manualreset, BOOL initialstate, char const *)
{
	EventHandleBox * box = new EventHandleBox;
	box->Header.Kind = HANDLE_EVENT;
	box->Event.Signalled = initialstate != FALSE;
	box->Event.ManualReset = manualreset != FALSE;
	return((HANDLE)box);
}

BOOL SetEvent(HANDLE handle)
{
	EventHandle * event = Event_Of(handle);
	std::lock_guard<std::mutex> lock(event->Mutex);
	event->Signalled = true;
	event->Condition.notify_all();
	return(TRUE);
}

BOOL ResetEvent(HANDLE handle)
{
	EventHandle * event = Event_Of(handle);
	std::lock_guard<std::mutex> lock(event->Mutex);
	event->Signalled = false;
	return(TRUE);
}

HANDLE CreateMutex(LPSECURITY_ATTRIBUTES, BOOL, char const *)
{
	_LastError = ERROR_SUCCESS;
	return(CreateEvent(nullptr, FALSE, TRUE, nullptr));
}

BOOL ReleaseMutex(HANDLE handle) { return(SetEvent(handle)); }

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
	if (handle == nullptr || ((HandleHeader *)handle)->Kind != HANDLE_EVENT) {
		return(WAIT_FAILED);
	}
	EventHandle * event = Event_Of(handle);
	std::unique_lock<std::mutex> lock(event->Mutex);
	auto ready = [event] { return(event->Signalled); };
	if (milliseconds == INFINITE) {
		event->Condition.wait(lock, ready);
	} else if (!event->Condition.wait_for(lock, std::chrono::milliseconds(milliseconds), ready)) {
		return(WAIT_TIMEOUT);
	}
	if (!event->ManualReset) {
		event->Signalled = false;
	}
	return(WAIT_OBJECT_0);
}

// The returned handle is signalled when the thread function returns.
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD, DWORD * id)
{
	HANDLE done = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	std::thread thread([start, parameter, done] {
		Stack_Prime(STACK_PRIME_THREAD);
		start(parameter);
		SetEvent(done);
	});
	if (id != nullptr) {
		*id = (DWORD)std::hash<std::thread::id>()(thread.get_id());
	}
	thread.detach();
	return(done);
}

UINT timeSetEvent(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT) { return(0); }
UINT timeKillEvent(UINT) { return(TIMERR_NOERROR); }

DWORD GetCurrentThreadId(void)
{
	return((DWORD)std::hash<std::thread::id>()(std::this_thread::get_id()));
}

DWORD GetCurrentProcessId(void) { return((DWORD)getpid()); }
HANDLE GetCurrentProcess(void) { return((HANDLE)(intptr_t)-1); }
BOOL SetProcessInformation(HANDLE, int, void *, DWORD) { return(TRUE); }

void InitializeCriticalSection(CRITICAL_SECTION * section) { section->Mutex = new std::recursive_mutex; }
void DeleteCriticalSection(CRITICAL_SECTION * section) { delete (std::recursive_mutex *)section->Mutex; section->Mutex = nullptr; }
void EnterCriticalSection(CRITICAL_SECTION * section) { ((std::recursive_mutex *)section->Mutex)->lock(); }
void LeaveCriticalSection(CRITICAL_SECTION * section) { ((std::recursive_mutex *)section->Mutex)->unlock(); }

LONG InterlockedIncrement(LONG volatile * value) { return(__atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST)); }
LONG InterlockedDecrement(LONG volatile * value) { return(__atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST)); }
LONG InterlockedExchange(LONG volatile * target, LONG value) { return(__atomic_exchange_n(target, value, __ATOMIC_SEQ_CST)); }

/*
** Memory and system
*/
void GlobalMemoryStatus(MEMORYSTATUS * status)
{
	memset(status, 0, sizeof(*status));
	status->dwLength = sizeof(*status);
	status->dwTotalPhys = (SIZE_T)256 * 1024 * 1024;
	status->dwAvailPhys = (SIZE_T)192 * 1024 * 1024;
	status->dwTotalVirtual = (SIZE_T)1024 * 1024 * 1024;
	status->dwAvailVirtual = (SIZE_T)768 * 1024 * 1024;
	status->dwTotalPageFile = status->dwTotalPhys;
	status->dwAvailPageFile = status->dwAvailPhys;
}

BOOL GetVersionEx(OSVERSIONINFO * info)
{
	info->dwMajorVersion = 10;
	info->dwMinorVersion = 0;
	info->dwBuildNumber = 19041;
	info->dwPlatformId = 2;
	info->szCSDVersion[0] = '\0';
	return(TRUE);
}

int GetSystemMetrics(int index)
{
	switch (index) {
		case SM_CXSCREEN: case SM_CXVIRTUALSCREEN: return(1920);
		case SM_CYSCREEN: case SM_CYVIRTUALSCREEN: return(1080);
		case SM_CXDRAG: case SM_CYDRAG: return(4);
		case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return(4);
		default: return(0);
	}
}

UINT GetDoubleClickTime(void) { return(500); }
UINT GetWindowsDirectory(char *, UINT) { return(0); }

unsigned int _controlfp(unsigned int, unsigned int) { return(0); }
uint64_t __rdtsc(void) { return(Microseconds() * 1000); }
void __cpuid(int info[4], int) { info[0] = info[1] = info[2] = info[3] = 0; }

/*
** Registry: there is none.
*/
LONG RegOpenKeyEx(HKEY, char const *, DWORD, DWORD, HKEY * result) { if (result != nullptr) *result = nullptr; return(ERROR_FILE_NOT_FOUND); }
LONG RegQueryValueEx(HKEY, char const *, DWORD *, DWORD *, BYTE *, DWORD *) { return(ERROR_FILE_NOT_FOUND); }
LONG RegCloseKey(HKEY) { return(ERROR_SUCCESS); }

/*
** Windows, messages and input: SDL does this work.
*/
BOOL PeekMessage(MSG *, HWND, UINT, UINT, UINT) { return(FALSE); }
BOOL GetMessage(MSG *, HWND, UINT, UINT) { return(FALSE); }
BOOL TranslateMessage(MSG const *) { return(FALSE); }
LRESULT DispatchMessage(MSG const *) { return(0); }
BOOL PostMessage(HWND, UINT, WPARAM, LPARAM) { return(TRUE); }
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return(0); }
int ShowCursor(BOOL show) { return(show ? 0 : -1); }
SHORT GetKeyState(int) { return(0); }
SHORT GetAsyncKeyState(int) { return(0); }
HKL GetKeyboardLayout(DWORD) { return(nullptr); }
HWND GetCapture(void) { return(nullptr); }
HWND SetCapture(HWND) { return(nullptr); }
BOOL ReleaseCapture(void) { return(TRUE); }
HWND SetFocus(HWND) { return(nullptr); }
BOOL InvalidateRect(HWND, RECT const *, BOOL) { return(TRUE); }
BOOL GetClientRect(HWND, RECT * rect) { memset(rect, 0, sizeof(*rect)); return(FALSE); }

BOOL SetRect(RECT * rect, int left, int top, int right, int bottom)
{
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
	return(TRUE);
}

BOOL IntersectRect(RECT * dest, RECT const * a, RECT const * b)
{
	dest->left = std::max(a->left, b->left);
	dest->top = std::max(a->top, b->top);
	dest->right = std::min(a->right, b->right);
	dest->bottom = std::min(a->bottom, b->bottom);
	if (dest->right <= dest->left || dest->bottom <= dest->top) {
		memset(dest, 0, sizeof(*dest));
		return(FALSE);
	}
	return(TRUE);
}

BOOL PtInRect(RECT const * rect, POINT point)
{
	return(point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom);
}

void InitCommonControls(void) {}

/*
** GDI: no device contexts.
*/
HDC GetDC(HWND) { return(nullptr); }
int ReleaseDC(HWND, HDC) { return(0); }
HDC CreateCompatibleDC(HDC) { return(nullptr); }
BOOL DeleteDC(HDC) { return(FALSE); }
HGDIOBJ SelectObject(HDC, HGDIOBJ) { return(nullptr); }
BOOL DeleteObject(HGDIOBJ) { return(FALSE); }
HFONT CreateFont(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, char const *) { return(nullptr); }
BOOL TextOut(HDC, int, int, char const *, int) { return(FALSE); }
BOOL GetTextExtentPoint32(HDC, char const *, int, SIZE * size) { size->cx = size->cy = 0; return(FALSE); }
COLORREF SetTextColor(HDC, COLORREF) { return(0); }
COLORREF SetBkColor(HDC, COLORREF) { return(0); }
int SetBkMode(HDC, int) { return(0); }
UINT SetTextAlign(HDC, UINT) { return(0); }
int SetStretchBltMode(HDC, int) { return(0); }
HBITMAP CreateDIBSection(HDC, BITMAPINFO const *, UINT, void ** bits, HANDLE, DWORD) { if (bits != nullptr) *bits = nullptr; return(nullptr); }
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return(FALSE); }
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return(FALSE); }
BOOL GdiFlush(void) { return(TRUE); }
int GetObject(HGDIOBJ, int, void *) { return(0); }

/*
** Resources: the strings come from the table rcstrings.py made from language.rc.
*/
static int const LANGUAGE_MODULE = 1;

HMODULE LoadLibrary(char const * name)
{
	if (name != nullptr && strcasecmp(name, "Language.dll") == 0) {
		return((HMODULE)&LANGUAGE_MODULE);
	}
	_LastError = ERROR_FILE_NOT_FOUND;
	return(nullptr);
}

BOOL FreeLibrary(HMODULE) { return(TRUE); }
void * GetProcAddress(HMODULE, char const *) { return(nullptr); }
HMODULE GetModuleHandle(char const *) { return(nullptr); }

int LoadString(HINSTANCE, UINT id, char * buffer, int size)
{
	if (buffer == nullptr || size <= 0) {
		return(0);
	}
	RCString const * end = RCStrings + RCStringCount;
	RCString const * found = std::lower_bound(RCStrings, end, id, [](RCString const & entry, UINT value) { return(entry.Id < value); });
	if (found == end || found->Id != id) {
		buffer[0] = '\0';
		return(0);
	}
	snprintf(buffer, (size_t)size, "%s", found->Text);
	return((int)strlen(buffer));
}

DWORD GetFileVersionInfoSize(char const *, DWORD * handle) { if (handle != nullptr) *handle = 0; return(0); }
BOOL GetFileVersionInfo(char const *, DWORD, DWORD, void *) { return(FALSE); }
BOOL VerQueryValue(void const *, char const *, void ** buffer, UINT * length) { if (buffer != nullptr) *buffer = nullptr; if (length != nullptr) *length = 0; return(FALSE); }

/*
** Code pages. The "ANSI" code page is UTF-8; Windows-1252 is converted for the text drawing
** code that asks for it by number.
*/
static char32_t const Windows_1252_High[32] = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

UINT GetACP(void) { return(CP_UTF8); }

static int Encode_UTF8(char32_t code, char * out)
{
	if (code < 0x80) { out[0] = (char)code; return(1); }
	if (code < 0x800) { out[0] = (char)(0xC0 | (code >> 6)); out[1] = (char)(0x80 | (code & 0x3F)); return(2); }
	if (code < 0x10000) { out[0] = (char)(0xE0 | (code >> 12)); out[1] = (char)(0x80 | ((code >> 6) & 0x3F)); out[2] = (char)(0x80 | (code & 0x3F)); return(3); }
	out[0] = (char)(0xF0 | (code >> 18)); out[1] = (char)(0x80 | ((code >> 12) & 0x3F)); out[2] = (char)(0x80 | ((code >> 6) & 0x3F)); out[3] = (char)(0x80 | (code & 0x3F)); return(4);
}

int WideCharToMultiByte(UINT page, DWORD, wchar_t const * wide, int widelength, char * narrow, int narrowlength, char const * defaultchar, BOOL * useddefault)
{
	if (useddefault != nullptr) *useddefault = FALSE;
	if (widelength < 0) {
		widelength = 0;
		while (wide[widelength] != 0) widelength++;
		widelength++; // the terminator is converted too
	}
	std::string result;
	for (int index = 0; index < widelength; index++) {
		char32_t code = (char32_t)wide[index];
		if (page == CP_UTF8 || page == CP_ACP) {
			char bytes[4];
			result.append(bytes, (size_t)Encode_UTF8(code, bytes));
			continue;
		}
		// Windows-1252 and the other single-byte pages.
		int byte = -1;
		if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
			byte = (int)code;
		} else {
			for (int high = 0; high < 32; high++) {
				if (Windows_1252_High[high] == code && code != 0) {
					byte = 0x80 + high;
					break;
				}
			}
		}
		if (byte < 0) {
			byte = (defaultchar != nullptr) ? (unsigned char)*defaultchar : '?';
			if (useddefault != nullptr) *useddefault = TRUE;
		}
		result.push_back((char)byte);
	}
	if (narrowlength == 0) {
		return((int)result.size());
	}
	if ((int)result.size() > narrowlength) {
		return(0);
	}
	memcpy(narrow, result.data(), result.size());
	return((int)result.size());
}

int MultiByteToWideChar(UINT page, DWORD, char const * narrow, int narrowlength, wchar_t * wide, int widelength)
{
	if (narrowlength < 0) {
		narrowlength = (int)strlen(narrow) + 1;
	}
	std::wstring result;
	for (int index = 0; index < narrowlength;) {
		unsigned char byte = (unsigned char)narrow[index];
		if (page != CP_UTF8 && page != CP_ACP) {
			char32_t code = (byte >= 0x80 && byte < 0xA0) ? Windows_1252_High[byte - 0x80] : byte;
			result.push_back((wchar_t)code);
			index++;
			continue;
		}
		char32_t code;
		int extra;
		if (byte < 0x80) { code = byte; extra = 0; }
		else if ((byte & 0xE0) == 0xC0) { code = byte & 0x1F; extra = 1; }
		else if ((byte & 0xF0) == 0xE0) { code = byte & 0x0F; extra = 2; }
		else { code = byte & 0x07; extra = 3; }
		index++;
		for (int more = 0; more < extra && index < narrowlength; more++, index++) {
			code = (code << 6) | ((unsigned char)narrow[index] & 0x3F);
		}
		result.push_back((wchar_t)code);
	}
	if (widelength == 0) {
		return((int)result.size());
	}
	if ((int)result.size() > widelength) {
		return(0);
	}
	memcpy(wide, result.data(), result.size() * sizeof(wchar_t));
	return((int)result.size());
}

/*
** Messages to the user and the debugger
*/
int MessageBox(HWND, char const * text, char const * caption, UINT type)
{
	fprintf(stderr, "%s: %s\n", caption != nullptr ? caption : "OpenTS", text != nullptr ? text : "");
	if ((type & 0xF) == MB_YESNO || (type & 0xF) == MB_YESNOCANCEL) {
		return(IDNO);
	}
	return(IDOK);
}

void OutputDebugString(char const * text)
{
	fputs(text, stderr);
}

BOOL IsDebuggerPresent(void) { return(FALSE); }
void DebugBreak(void) { __builtin_trap(); }

int _kbhit(void) { return(0); }
int _getch(void) { return(getchar()); }
