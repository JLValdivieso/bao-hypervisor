/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef __ARCH_EMUL_H__
#define __ARCH_EMUL_H__

/* The order of the operations can NOT be modified */
enum emul_arch_bwop {
    EMUL_ARCH_BWOP_NO,
    EMUL_ARCH_BWOP_SET1,
    EMUL_ARCH_BWOP_NOT1,
    EMUL_ARCH_BWOP_CLR1,
    EMUL_ARCH_BWOP_TST1
};

struct emul_access_arch {
    enum emul_arch_bwop op;
    unsigned long byte_mask;
};

struct emul_access;
unsigned long emul_arch_bwop_get_acc_bitop_mask(struct emul_access* acc);
void emul_arch_bwop_set_gmpsw(unsigned long cur_val, unsigned long bitop_mask);

unsigned long emul_arch_bwop_set_val(struct emul_access* acc, unsigned long cur_val,
    unsigned long bitop_mask);

#endif /* __ARCH_EMUL_H__ */
