/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#ifndef __ARCH_EMUL_H__
#define __ARCH_EMUL_H__

#include <bao.h>

/* The order of the operations can NOT be modified */
enum emul_arch_bwop {
    EMUL_ARCH_BWOP_NO,
    EMUL_ARCH_BWOP_SET1,
    EMUL_ARCH_BWOP_NOT1,
    EMUL_ARCH_BWOP_CLR1,
    EMUL_ARCH_BWOP_TST1
};

struct emul_access_arch {
    enum emul_arch_bwop bwop;
    uint8_t bit;
};


bool emul_arch_is_bwop(struct emul_access_arch* acc);
uint8_t emul_arch_bwop_emul_acc(struct emul_access_arch* acc, uint8_t cur_val);

#endif /* __ARCH_EMUL_H__ */
