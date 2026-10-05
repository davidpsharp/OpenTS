/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** A crash report for RISC OS. UnixLib's own backtrace follows the APCS frame chain, which
** clang's code does not keep, so this prints the registers at the fault and every word on
** the stack that looks like a return address into the program. Feed those to addr2line.
*/

#include "crashreport.h"

#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" char __executable_start[];
extern "C" char etext[];

// Signal-safe: snprintf into a buffer and write() it, with no locks or stdio streams.
static void Say(char const * format, ...)
{
	char text[320];
	va_list args;
	va_start(args, format);
	int const length = vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (length > 0) {
		write(2, text, (size_t)(length < (int)sizeof(text) ? length : (int)sizeof(text) - 1));
	}
}

static void Report(int signal, siginfo_t * info, void * context)
{
	struct sigcontext const * regs = (struct sigcontext const *)context;
	std::uintptr_t const low = (std::uintptr_t)__executable_start;
	std::uintptr_t const high = (std::uintptr_t)etext;

	Say("Crash: signal %d, address %p\n", signal, info != nullptr ? info->si_addr : nullptr);
	if (regs != nullptr) {
		Say("Crash: pc %08lx lr %08lx sp %08lx fault %08lx\n", regs->arm_pc, regs->arm_lr, regs->arm_sp, regs->fault_address);
		Say("Crash: r0 %08lx r1 %08lx r2 %08lx r3 %08lx r4 %08lx r5 %08lx r6 %08lx r7 %08lx\n",
			regs->arm_r0, regs->arm_r1, regs->arm_r2, regs->arm_r3, regs->arm_r4, regs->arm_r5, regs->arm_r6, regs->arm_r7);
		Say("Crash: r8 %08lx r9 %08lx r10 %08lx fp %08lx ip %08lx\n", regs->arm_r8, regs->arm_r9, regs->arm_r10, regs->arm_fp, regs->arm_ip);

		// The return addresses among the 2048 words above the stack pointer.
		std::uint32_t const * stack = (std::uint32_t const *)regs->arm_sp;
		char line[256];
		int length = 0;
		int found = 0;
		for (int index = 0; index < 2048 && found < 48; index++) {
			std::uintptr_t const word = stack[index];
			if (word >= low && word < high && (word & 1) == 0) {
				length += snprintf(line + length, sizeof(line) - length, " %08lx", (unsigned long)word);
				found++;
				if (length > 200) {
					Say("Crash: stack%s\n", line);
					length = 0;
				}
			}
		}
		if (length > 0) {
			Say("Crash: stack%s\n", line);
		}
	}
	_exit(1);
}

void Crash_Report_Install(void)
{

	struct sigaction action = {};
	action.sa_sigaction = Report;
	action.sa_flags = SA_SIGINFO;
	sigemptyset(&action.sa_mask);
	sigaction(SIGSEGV, &action, nullptr);
	sigaction(SIGBUS, &action, nullptr);
	sigaction(SIGILL, &action, nullptr);
	sigaction(SIGFPE, &action, nullptr);
}
