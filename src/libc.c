// The few C library routines GCC may call on its own (struct and array
// initialisation), since the project links without newlib.
#include <stddef.h>

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

// Freestanding LLVM EABI aggregate-copy aliases. GCC remains on memcpy.
void __aeabi_memcpy(void *dst,const void *src,size_t n){ memcpy(dst,src,n); }
void __aeabi_memcpy4(void *dst,const void *src,size_t n){ memcpy(dst,src,n); }
void __aeabi_memcpy8(void *dst,const void *src,size_t n){ memcpy(dst,src,n); }
