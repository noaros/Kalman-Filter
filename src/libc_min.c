/**
 * @file    libc_min.c
 * @brief   The only two C library functions this firmware needs.
 *
 * We link with -nostdlib, so no newlib / libc is linked. However, the C
 * standard allows the compiler to emit calls to memcpy() and memset() on
 * its own -- for example to zero a large local struct or copy a struct by
 * value -- even when the source never calls them. Providing these keeps
 * the link working in those cases.
 *
 * Note the build flag -fno-tree-loop-distribute-patterns: without it, GCC
 * may notice that the loop in memset() "looks like memset" and replace it
 * with a call to... memset(), producing infinite recursion.
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dst;
}
