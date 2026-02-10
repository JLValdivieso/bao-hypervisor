/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */


#include <asm_defs.h>

.section .text
.global clear_rwx_regions
/**
 * Clear RWX memory regions defined in platform
 * r15 = plat_vma   (pointer value as linked for VMA)
 * r16 = data_vma   (start of .data at VMA)
 * r17 = data_lma   (start of .data at LMA)
 */
clear_rwx_regions:
    /* Compute delta = data_lma - data_vma  (r20 = delta) */
    mov     r17, r20
    sub     r16, r20              /* r20 = r17 - r16 */

    /* Convert platform pointer to LMA: plat_lma = plat_vma + delta (r20) */
    mov     r15, r21
    add     r20, r21             /* r21 = r15 + delta */

    /* Load region_num (use r24 as loop counter) */
    ld.w    PLATFORM_REGION_NUM_OFF[r21], r24
    cmp     r0, r24
    be      _done                /* if (region_num == 0) return */

    /* Load regions pointer (stored as VMA pointer), convert to LMA */
    ld.w    PLATFORM_REGIONS_PTR_OFF[r21], r18   /* r18 = regions_vma */
    add     r20, r18                         /* r18 = regions_lma */

    /* r25 = sizeof(struct mem_region) */
    mov     MEM_REGION_SIZEOF, r25

_loop_region:
    /* region->perms */
    ld.w    MEM_REGION_PERMS_OFF[r18], r26
    /* Assumes RWX permission is 0x0 */
    cmp     r0, r26
    bne     _skip_clear           /* perms != MEM_RWX => skip */

    /* region->base and region->size */
    ld.w    MEM_REGION_BASE_OFF[r18], r22    /* r22 = base */
    ld.w    MEM_REGION_SIZE_OFF[r18], r23    /* r23 = size */
    cmp     r0, r23
    be      _skip_clear

    /* end = base + size */
    add     r22, r23              /* r23 = end */

    /* Clear [r12, r13) */
    br      _clear_range

_skip_clear:
    /* next region struct */
    add     r25, r18

    /* decrement count and loop */
    addi    -1, r24, r24
    cmp     r0, r24
    bne     _loop_region

_done:
    jmp     [lp]

/**
 * Clears words in [r22, r23]
 * r22 = start
 * r23 = end
 */
_clear_range:
    cmp     r22, r23
    be      _cr_done
    st.w    r0, 0[r22]
    addi    4, r22, r22
    br      _clear_range

_cr_done:
    br      _skip_clear

