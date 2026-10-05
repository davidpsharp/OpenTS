#pragma once
#include "windows.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
long filelength(int handle);
#define _filelength filelength
#define _open open
#define _close close
#define _read read
#define _write write
#define _lseek lseek
