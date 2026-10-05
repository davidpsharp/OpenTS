/*
** Installs a handler that logs where the program crashed. Only RISC OS has one; elsewhere a
** debugger does the job better.
*/
#pragma once

#ifdef __riscos__
void Crash_Report_Install(void);
#else
inline void Crash_Report_Install(void) {}
#endif
