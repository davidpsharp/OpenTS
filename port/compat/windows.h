/*
** Win32 declarations for the POSIX build (macOS and RISC OS). Only what the game uses is
** here; port/compat/win32.cpp implements the functions. GDI, registry and window-message
** calls report failure, because the SDL code replaces them.
*/
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "msvcrt.h"

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned char BOOLEAN;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef unsigned int UINT;
typedef int INT;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef uint64_t DWORD64;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef short SHORT;
typedef unsigned short USHORT;
typedef float FLOAT;
typedef void VOID;
typedef void* PVOID;
typedef void* LPVOID;
typedef void const* LPCVOID;
typedef char* LPSTR;
typedef char const* LPCSTR;
typedef char* PSTR;
typedef char const* PCSTR;
typedef char TCHAR;
typedef char* LPTSTR;
typedef char const* LPCTSTR;
typedef wchar_t WCHAR;
typedef wchar_t* LPWSTR;
typedef wchar_t const* LPCWSTR;
typedef DWORD* LPDWORD;
typedef DWORD* PDWORD;
typedef BYTE* LPBYTE;
typedef BYTE* PBYTE;
typedef LONG* LPLONG;
typedef LONG* PLONG;
typedef BOOL* LPBOOL;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef long HRESULT;
typedef DWORD COLORREF;
typedef WORD ATOM;
typedef size_t SIZE_T;

typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE HWND;
typedef HANDLE HINSTANCE;
typedef HANDLE HMODULE;
typedef HANDLE HDC;
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HBRUSH;
typedef HANDLE HPEN;
typedef HANDLE HMENU;
typedef HANDLE HFONT;
typedef HANDLE HBITMAP;
typedef HANDLE HGDIOBJ;
typedef HANDLE HPALETTE;
typedef HANDLE HRGN;
typedef HANDLE HKEY;
typedef HANDLE HGLOBAL;
typedef HANDLE HRSRC;
typedef HANDLE HMONITOR;
typedef HANDLE HKL;
typedef HANDLE HIMC;
typedef HKEY* PHKEY;

#define WINAPI
#define CALLBACK
#define APIENTRY
#define PASCAL
#define FAR
#define NEAR
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define __stdcall
#define __cdecl
#define _cdecl
#define __fastcall
#define __forceinline inline __attribute__((always_inline))
#define UNREFERENCED_PARAMETER(p) ((void)(p))

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef _MAX_PATH
#define _MAX_PATH 260
#endif
#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#endif
#ifndef _MAX_DIR
#define _MAX_DIR 256
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif

#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_NO_MORE_FILES 18L
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu

#define MAKEWORD(a, b) ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT, *PRECT;
typedef RECT const* LPCRECT;

typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT, *PPOINT;

typedef struct tagSIZE {
    LONG cx, cy;
} SIZE, *LPSIZE, *PSIZE;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *LPFILETIME, *PFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME, *PSYSTEMTIME;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

typedef struct tagPALETTEENTRY {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

/* Time */
DWORD timeGetTime(void);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void Sleep(DWORD milliseconds);
void GetLocalTime(SYSTEMTIME* time);
void GetSystemTime(SYSTEMTIME* time);
void GetSystemTimeAsFileTime(FILETIME* time);
BOOL FileTimeToSystemTime(FILETIME const* file, SYSTEMTIME* system);
BOOL FileTimeToLocalFileTime(FILETIME const* file, FILETIME* local);
BOOL SystemTimeToFileTime(SYSTEMTIME const* system, FILETIME* file);
LONG CompareFileTime(FILETIME const* a, FILETIME const* b);
#define TIMERR_NOERROR 0
#define TIME_PERIODIC 1
#define TIME_ONESHOT 0
#define TIME_CALLBACK_FUNCTION 0
typedef void(CALLBACK* LPTIMECALLBACK)(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR dw1, DWORD_PTR dw2);
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);
UINT timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags);
UINT timeKillEvent(UINT id);

/* Files */
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x1
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_ATTRIBUTE_SYSTEM 0x4
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATA, *LPWIN32_FIND_DATAA;

HANDLE CreateFile(char const* name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templatefile);
#define CreateFileA CreateFile
BOOL ReadFile(HANDLE file, void* buffer, DWORD size, DWORD* read, void* overlapped);
BOOL WriteFile(HANDLE file, void const* buffer, DWORD size, DWORD* written, void* overlapped);
BOOL CloseHandle(HANDLE handle);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG* high, DWORD method);
DWORD GetFileSize(HANDLE file, DWORD* high);
BOOL GetFileTime(HANDLE file, FILETIME* creation, FILETIME* access, FILETIME* write);
BOOL FlushFileBuffers(HANDLE file);
HANDLE FindFirstFile(char const* pattern, WIN32_FIND_DATA* data);
BOOL FindNextFile(HANDLE find, WIN32_FIND_DATA* data);
BOOL FindClose(HANDLE find);
#define FindFirstFileA FindFirstFile
#define FindNextFileA FindNextFile
DWORD GetFileAttributes(char const* name);
#define GetFileAttributesA GetFileAttributes
BOOL CreateDirectory(char const* name, void* security);
#define CreateDirectoryA CreateDirectory
BOOL RemoveDirectory(char const* name);
BOOL DeleteFile(char const* name);
#define DeleteFileA DeleteFile
BOOL MoveFile(char const* from, char const* to);
BOOL CopyFile(char const* from, char const* to, BOOL failifexists);
DWORD GetCurrentDirectory(DWORD size, char* buffer);
BOOL SetCurrentDirectory(char const* name);
DWORD GetModuleFileName(HMODULE module, char* buffer, DWORD size);
#define GetModuleFileNameA GetModuleFileName
DWORD GetFullPathName(char const* name, DWORD size, char* buffer, char** filepart);
DWORD GetLastError(void);
void SetLastError(DWORD error);

/* Threads */
typedef DWORD(WINAPI* LPTHREAD_START_ROUTINE)(LPVOID parameter);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES security, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags, DWORD* id);
HANDLE CreateEvent(LPSECURITY_ATTRIBUTES security, BOOL manualreset, BOOL initialstate, char const* name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateMutex(LPSECURITY_ATTRIBUTES security, BOOL initialowner, char const* name);
BOOL ReleaseMutex(HANDLE mutex);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessId(void);
typedef struct _CRITICAL_SECTION {
    void* Mutex;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;
void InitializeCriticalSection(CRITICAL_SECTION* section);
void DeleteCriticalSection(CRITICAL_SECTION* section);
void EnterCriticalSection(CRITICAL_SECTION* section);
void LeaveCriticalSection(CRITICAL_SECTION* section);
LONG InterlockedIncrement(LONG volatile* value);
LONG InterlockedDecrement(LONG volatile* value);
LONG InterlockedExchange(LONG volatile* target, LONG value);

/* Memory and system */
typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
void GlobalMemoryStatus(MEMORYSTATUS* status);
typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, *LPOSVERSIONINFO;
BOOL GetVersionEx(OSVERSIONINFO* info);
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
int GetSystemMetrics(int index);

/* Registry: always reports that the key is missing. */
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002)
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_SZ 1
#define REG_DWORD 4
LONG RegOpenKeyEx(HKEY key, char const* subkey, DWORD options, DWORD access, HKEY* result);
LONG RegQueryValueEx(HKEY key, char const* name, DWORD* reserved, DWORD* type, BYTE* data, DWORD* size);
LONG RegCloseKey(HKEY key);
#define RegOpenKeyExA RegOpenKeyEx
#define RegQueryValueExA RegQueryValueEx

/* Windows and messages: the SDL code replaces these, so they do nothing. */
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_USER 0x0400
#define WM_APP 0x8000
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
BOOL PeekMessage(MSG* msg, HWND window, UINT first, UINT last, UINT remove);
BOOL GetMessage(MSG* msg, HWND window, UINT first, UINT last);
BOOL TranslateMessage(MSG const* msg);
LRESULT DispatchMessage(MSG const* msg);
BOOL PostMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
int ShowCursor(BOOL show);
SHORT GetKeyState(int key);
SHORT GetAsyncKeyState(int key);
HKL GetKeyboardLayout(DWORD thread);
BOOL GetClientRect(HWND window, RECT* rect);
BOOL SetRect(RECT* rect, int left, int top, int right, int bottom);
BOOL IntersectRect(RECT* dest, RECT const* a, RECT const* b);
BOOL PtInRect(RECT const* rect, POINT point);

/* GDI: the device contexts are never available. */
#define TRANSPARENT 1
#define OPAQUE 2
#define FW_NORMAL 400
#define FW_BOLD 700
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY 4
#define DEFAULT_PITCH 0
#define FIXED_PITCH 1
#define VARIABLE_PITCH 2
#define FF_DONTCARE 0
#define FF_SWISS 0x20
#define FF_MODERN 0x30
#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
HFONT CreateFont(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strikeout, DWORD charset, DWORD outprecision, DWORD clipprecision, DWORD quality, DWORD pitchandfamily, char const* face);
#define CreateFontA CreateFont
BOOL TextOut(HDC dc, int x, int y, char const* text, int length);
#define TextOutA TextOut
BOOL GetTextExtentPoint32(HDC dc, char const* text, int length, SIZE* size);
#define GetTextExtentPoint32A GetTextExtentPoint32
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF SetBkColor(HDC dc, COLORREF color);
int SetBkMode(HDC dc, int mode);
HBITMAP CreateDIBSection(HDC dc, BITMAPINFO const* info, UINT usage, void** bits, HANDLE section, DWORD offset);
BOOL BitBlt(HDC dest, int x, int y, int width, int height, HDC source, int sx, int sy, DWORD rop);
BOOL GdiFlush(void);

/* Resources */
int LoadString(HINSTANCE instance, UINT id, char* buffer, int size);
#define LoadStringA LoadString
HMODULE LoadLibrary(char const* name);
#define LoadLibraryA LoadLibrary
BOOL FreeLibrary(HMODULE module);
void* GetProcAddress(HMODULE module, char const* name);
HMODULE GetModuleHandle(char const* name);
#define GetModuleHandleA GetModuleHandle

/* Messages to the user */
#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_RETRYCANCEL 0x5
#define MB_YESNO 0x4
#define MB_YESNOCANCEL 0x3
#define MB_ICONERROR 0x10
#define MB_ICONHAND 0x10
#define MB_ICONSTOP 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30
#define MB_ICONEXCLAMATION 0x30
#define MB_ICONINFORMATION 0x40
#define MB_ICONASTERISK 0x40
#define MB_SYSTEMMODAL 0x1000
#define MB_TASKMODAL 0x2000
#define MB_SETFOREGROUND 0x10000
#define MB_TOPMOST 0x40000
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
int MessageBox(HWND window, char const* text, char const* caption, UINT type);
#define MessageBoxA MessageBox
void OutputDebugString(char const* text);
#define OutputDebugStringA OutputDebugString
BOOL IsDebuggerPresent(void);
void DebugBreak(void);

/* Keys */
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2

/* Process and power: nothing to do outside Windows. */
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
enum { ProcessPowerThrottling = 4 };
HANDLE GetCurrentProcess(void);
BOOL SetProcessInformation(HANDLE process, int infoclass, void* info, DWORD size);

/* More system metrics and input settings */
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXDRAG 68
#define SM_CYDRAG 69
UINT GetDoubleClickTime(void);
HWND GetCapture(void);
HWND SetCapture(HWND window);
BOOL ReleaseCapture(void);
HWND SetFocus(HWND window);
BOOL InvalidateRect(HWND window, RECT const* rect, BOOL erase);
LRESULT DefWindowProcW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
#define DefWindowProc DefWindowProcW
#define WM_CANCELMODE 0x001F
#define WM_CAPTURECHANGED 0x0215
#define WM_EXITSIZEMOVE 0x0232
#define WM_EXITMENULOOP 0x0212
#define WM_ENTERSIZEMOVE 0x0231
#define WM_ENTERMENULOOP 0x0211

/* File attributes, moves and dates */
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define MOVEFILE_REPLACE_EXISTING 0x1
#define MOVEFILE_WRITE_THROUGH 0x8
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
BOOL GetFileAttributesEx(char const* name, GET_FILEEX_INFO_LEVELS level, void* info);
#define GetFileAttributesExA GetFileAttributesEx
BOOL MoveFileEx(char const* from, char const* to, DWORD flags);
#define MoveFileExA MoveFileEx
#define LANG_USER_DEFAULT 0x0400
#define LOCALE_USER_DEFAULT 0x0400
#define DATE_SHORTDATE 0x1
#define TIME_NOMINUTESORSECONDS 0x1
#define TIME_NOSECONDS 0x2
int GetDateFormat(DWORD locale, DWORD flags, SYSTEMTIME const* time, char const* format, char* buffer, int size);
int GetTimeFormat(DWORD locale, DWORD flags, SYSTEMTIME const* time, char const* format, char* buffer, int size);
UINT GetWindowsDirectory(char* buffer, UINT size);
#define GetWindowsDirectoryA GetWindowsDirectory

/* Code pages: the POSIX build treats the "ANSI" code page as UTF-8. */
#define CP_ACP 0
#define CP_UTF8 65001
#define WC_NO_BEST_FIT_CHARS 0x400
UINT GetACP(void);
int WideCharToMultiByte(UINT page, DWORD flags, wchar_t const* wide, int widelength, char* narrow, int narrowlength, char const* defaultchar, BOOL* useddefault);
int MultiByteToWideChar(UINT page, DWORD flags, char const* narrow, int narrowlength, wchar_t* wide, int widelength);

/* Version resources: there are none. */
typedef struct tagVS_FIXEDFILEINFO {
    DWORD dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS, dwProductVersionMS, dwProductVersionLS;
} VS_FIXEDFILEINFO;
DWORD GetFileVersionInfoSize(char const* name, DWORD* handle);
BOOL GetFileVersionInfo(char const* name, DWORD handle, DWORD size, void* data);
BOOL VerQueryValue(void const* block, char const* subblock, void** buffer, UINT* length);

/* More GDI, used only where a device context was obtained, which never happens here. */
#define OUT_RASTER_PRECIS 6
#define PROOF_QUALITY 2
#define TA_LEFT 0
#define TA_CENTER 6
#define TA_RIGHT 2
#define TA_TOP 0
#define TA_BASELINE 24
#define COLORONCOLOR 3
#define HALFTONE 4
UINT SetTextAlign(HDC dc, UINT align);
int SetStretchBltMode(HDC dc, int mode);
BOOL StretchBlt(HDC dest, int x, int y, int width, int height, HDC source, int sx, int sy, int swidth, int sheight, DWORD rop);
int GetObject(HGDIOBJ object, int size, void* buffer);
typedef struct tagBITMAP {
    LONG bmType, bmWidth, bmHeight, bmWidthBytes;
    WORD bmPlanes, bmBitsPixel;
    void* bmBits;
} BITMAP;
typedef struct tagDIBSECTION {
    BITMAP dsBm;
    BITMAPINFOHEADER dsBmih;
    DWORD dsBitfields[3];
    HANDLE dshSection;
    DWORD dsOffset;
} DIBSECTION;

/* Media keys */
#define VK_SLEEP 0x5F
#define VK_VOLUME_MUTE 0xAD
#define VK_VOLUME_DOWN 0xAE
#define VK_VOLUME_UP 0xAF
#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define VK_MEDIA_STOP 0xB2
#define VK_MEDIA_PLAY_PAUSE 0xB3

/* Disc space */
BOOL GetDiskFreeSpaceEx(char const* directory, ULARGE_INTEGER* available, ULARGE_INTEGER* total, ULARGE_INTEGER* free);
#define GetDiskFreeSpaceExA GetDiskFreeSpaceEx
