/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <vbootctrl.h>
#include <arch/emul.h>

static bool vbootctrl_emul_handler(struct emul_access* acc)
{
    struct vcpu* vcpu = cpu()->vcpu;
    unsigned long notify = 0;
    struct vcpu* waking_vcpu = NULL;

    if(acc->addr != platform.arch.bootctrl_addr && acc->width != 32){
        /* ignore access */
        return true;
    }

    /* Translate access */
    /* TODO does this register support bitwise instructions? */
    if (emul_arch_is_bwop(&acc->arch)) {
        /* this access is fairly unique, so it's not practical to put behind
         * arch emul */
        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            if ((1U << i) & acc->arch.bit) {
                waking_vcpu = vm_get_vcpu(vcpu->vm, i);
                if (waking_vcpu != NULL) {
                    break;
                }
            }
        }

        if(waking_vcpu != NULL){
            unsigned long psw = srs_gmpsw_read();
            if (waking_vcpu->arch.started) {
                srs_gmpsw_write(psw & ~PSW_Z);
            } else {
                srs_gmpsw_write(psw | PSW_Z);
            }
            if (!waking_vcpu->arch.started) {
                notify |= (1UL << waking_vcpu->phys_id);
                switch (acc->arch.bwop) {
                    case EMUL_ARCH_BWOP_SET1:
                        waking_vcpu->arch.started = true;
                        break;
                    case EMUL_ARCH_BWOP_NOT1:
                        waking_vcpu->arch.started = true;
                        break;
                        /* CLR1 accesses are ignored */
                        /* TST1 only modifies the PSW.Z flag */
                    default:
                        break;
                }
            }
        }
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            if ((1U << i) & val) {
                waking_vcpu = vm_get_vcpu(vcpu->vm, i);
                if (waking_vcpu == NULL) {
                    continue;
                }
                if (!waking_vcpu->arch.started) {
                    notify |= (1UL << waking_vcpu->phys_id);
                    waking_vcpu->arch.started = true;
                }
            }
        }
    } else {
        unsigned long val = 0;
        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            struct vcpu* awake_vcpu = vm_get_vcpu(vcpu->vm, i);
            if (awake_vcpu == NULL) {
                continue;
            }
            if (awake_vcpu->arch.started) {
                val |= 1UL << i;
            }
        }
        vcpu_writereg(vcpu, acc->reg, val);
    }

    /* Notify physical CPUs, if any */
    if (notify != 0) {
        for (cpuid_t c = 0; c < platform.cpu_num; c++) {
            if (notify & (1UL << c)) {
                interrupts_cpu_sendipi(c);
            }
        }
    }

    return true;
}

void vbootctrl_init(struct vm* vm)
{
    if (cpu()->id == vm->master) {
        vm->arch.bootctrl_emul = (struct emul_mem){
            .va_base = platform.arch.bootctrl_addr,
            /* BOOTCTRL is 4 bytes only */
            .size = ALIGN(4, PAGE_SIZE),
            .handler = vbootctrl_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.bootctrl_emul);
    }
}
