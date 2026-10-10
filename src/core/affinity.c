/* affinity.c - see affinity.h. Raw bit masks avoid depending on SDK macro names:
 * bit N is core N. 0xF enables cores 0-3 (needs the unlock mod for core 3); when the
 * kernel rejects it we fall back to the stock 0x7. */
#include "core/affinity.h"

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>

void affinity_use_all_cores(void)
{
    SceUID me = sceKernelGetThreadId();
    if (sceKernelChangeThreadCpuAffinityMask(me, 0xF) < 0)
        sceKernelChangeThreadCpuAffinityMask(me, 0x7);
}

#endif
