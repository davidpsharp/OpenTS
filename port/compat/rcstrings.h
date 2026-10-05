#pragma once
// The game strings, sorted by number (see port/tools/rcstrings.py).
struct RCString
{
	unsigned Id;
	char const * Text;
};
extern RCString const RCStrings[];
extern unsigned const RCStringCount;
