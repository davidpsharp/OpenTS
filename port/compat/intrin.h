#pragma once
#include <cstdint>
uint64_t __rdtsc(void);
void __cpuid(int info[4], int function);
