/*
** The ARM EABI memory helpers that clang calls on its bare-metal ARM target. GCCSDK's
** runtime does not provide them, so they pass straight to the C library.
*/
#include <stddef.h>
#include <string.h>

void __aeabi_memcpy(void *dest, const void *src, size_t n) { memcpy(dest, src, n); }
void __aeabi_memcpy4(void *dest, const void *src, size_t n) { memcpy(dest, src, n); }
void __aeabi_memcpy8(void *dest, const void *src, size_t n) { memcpy(dest, src, n); }
void __aeabi_memmove(void *dest, const void *src, size_t n) { memmove(dest, src, n); }
void __aeabi_memmove4(void *dest, const void *src, size_t n) { memmove(dest, src, n); }
void __aeabi_memmove8(void *dest, const void *src, size_t n) { memmove(dest, src, n); }
/* The EABI puts the length before the value. */
void __aeabi_memset(void *dest, size_t n, int c) { memset(dest, c, n); }
void __aeabi_memset4(void *dest, size_t n, int c) { memset(dest, c, n); }
void __aeabi_memset8(void *dest, size_t n, int c) { memset(dest, c, n); }
void __aeabi_memclr(void *dest, size_t n) { memset(dest, 0, n); }
void __aeabi_memclr4(void *dest, size_t n) { memset(dest, 0, n); }
void __aeabi_memclr8(void *dest, size_t n) { memset(dest, 0, n); }
