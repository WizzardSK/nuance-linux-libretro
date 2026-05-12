// intrin.h stub for Linux
// MSVC intrinsics - GCC/Clang equivalents
#ifndef _COMPAT_INTRIN_H
#define _COMPAT_INTRIN_H

#include <x86intrin.h>

// gcc's <ia32intrin.h> defines _lrotr/_lrotl as MACROS that map to
// __rorq/__rolq on LP64 (64-bit Linux) - i.e. 64-bit rotate. NUON code
// expects 32-bit rotate semantics, so a uint32 value zero-extended to
// 64-bit gets rotated wrongly. Force-redefine to 32-bit rotates.
#undef _lrotr
#undef _lrotl
#define _lrotr(val, shift) ((unsigned int)(((unsigned int)(val) >> ((shift) & 31)) | ((unsigned int)(val) << ((32 - ((shift) & 31)) & 31))))
#define _lrotl(val, shift) ((unsigned int)(((unsigned int)(val) << ((shift) & 31)) | ((unsigned int)(val) >> ((32 - ((shift) & 31)) & 31))))

#ifndef __cpuid
static inline void __cpuid(int cpuinfo[4], int info) {
    __asm__ __volatile__(
        "xchg %%ebx, %%edi;"
        "cpuid;"
        "xchg %%ebx, %%edi;"
        :"=a" (cpuinfo[0]), "=D" (cpuinfo[1]), "=c" (cpuinfo[2]), "=d" (cpuinfo[3])
        :"0" (info)
    );
}
#endif

#endif
