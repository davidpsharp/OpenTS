/*
** The parts of the Microsoft C library the game uses outside Win32 calls. The POSIX build
** includes this in every file, as MSVC makes them available everywhere.
*/
#pragma once

#include <cstdarg>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <unistd.h>
#include <sys/timeb.h>

#define _strcmpi strcasecmp
#define stricmp strcasecmp
#define _stricmp strcasecmp
#define strnicmp strncasecmp
#define _strnicmp strncasecmp
#define TEXT(s) s

#define _strnicmp strncasecmp
#define _strdup strdup
#define lstrlen strlen
#define lstrcpy strcpy
#define lstrcpyn(d, s, n) (strncpy((d), (s), (n)), (d)[(n) - 1] = '\0', (d))
#define lstrcat strcat
#define lstrcmp strcmp
#define lstrcmpi strcasecmp
#define _access access
char* itoa(int value, char* buffer, int radix);
char* ltoa(long value, char* buffer, int radix);
char* ultoa(unsigned long value, char* buffer, int radix);
#define _itoa itoa
#define _ltoa ltoa
#define _ultoa ultoa
#define _vsnprintf vsnprintf
#define _snprintf snprintf
#define _unlink unlink
#define _getcwd getcwd
#define _chdir chdir
#define _rmdir rmdir
#define _fileno fileno


/* Floating-point control: the POSIX build leaves the defaults alone. */
unsigned int _controlfp(unsigned int newvalue, unsigned int mask);
#define _MCW_PC 0x30000
#define _PC_24 0x20000
#define _PC_53 0x10000
#define _MCW_RC 0x300
#define _RC_NEAR 0

#ifdef __riscos__
/* GCC 10's <cmath> leaves the float functions out of namespace std. */
#include <cmath>
namespace std {
using ::sinf; using ::cosf; using ::tanf; using ::asinf; using ::acosf; using ::atanf; using ::atan2f;
using ::sqrtf; using ::fabsf; using ::floorf; using ::ceilf; using ::roundf; using ::truncf; using ::fmodf;
using ::powf; using ::expf; using ::logf; using ::log10f; using ::log2f; using ::exp2f; using ::hypotf;
using ::fminf; using ::fmaxf; using ::copysignf; using ::lroundf; using ::modff; using ::ldexpf; using ::frexpf;
}
#endif
