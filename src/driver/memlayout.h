#pragma once

/* Start of extended memory */
#define EXTMEM  0x80000

/* Top physical memory */
#define PHYSTOP 0x3f000000

#define KSPACE_MASK 0xffff000000000000

/* Address where kernel is linked */
#define KERNLINK    (KSPACE_MASK + EXTMEM)

 /* Same as V2P, but without casts */
#define K2P_WO(x) ((x) - (KSPACE_MASK))

/* Same as P2V, but without casts */
#define P2K_WO(x) ((x) + (KSPACE_MASK))
