/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <config.h>
#include <string.h>
#include <vm.h>
#include <ipir.h>
#include <vintc.h>

void vm_arch_init(struct vm* vm, const struct vm_config* vm_config)
{
    UNUSED_ARG(vm_config);

    /* All VMs use MPID5 for memory protection */
    srs_mpid5_write(vm->id);

    vintc_init(vm);
    vipir_init(vm);
    vbootctrl_init(vm);
}

void vcpu_arch_init(struct vcpu* vcpu, struct vm* vm)
{
    UNUSED_ARG(vm);
    UNUSED_ARG(vcpu);
}

void vcpu_arch_reset(struct vcpu* vcpu, vaddr_t entry)
{
    struct vm* vm = vcpu->vm;

    memset(&vcpu->regs, 0, sizeof(struct arch_regs));

    vcpu_writepc(vcpu, entry);
    srs_eipc_write(entry);

    vcpu->arch.started = vcpu->id == 0 ? true : false;

    /* Bao fixes the VMID as SPID to isolate VM memory regions */
    srs_gmspid_write(vm->id);
    srs_gmspidlist_write(0x0);

    srs_gmmpm_write(GMMPM_GMPE);

    unsigned long eipswh = srs_eipswh_read() & ~EIPSWH_GPID_MASK;
    srs_eipswh_write(eipswh | (vm->id << EIPSWH_GPID_OFF));

    unsigned long fepswh = srs_fepswh_read() & ~FEPSWH_GPID_MASK;
    srs_fepswh_write(fepswh | (vm->id << FEPSWH_GPID_OFF));

    srs_gmpeid_write(vcpu->id);

    /* clear guest-context exception registers */
    srs_gmeipc_write(0x0);
    srs_gmfepc_write(0x0);
    srs_gmmea_write(0x0);
    srs_gmmei_write(0x0);
    srs_gmeiic_write(0x0);
    srs_gmfeic_write(0x0);

    vintc_vcpu_reset(vcpu);
}

bool vcpu_arch_is_on(struct vcpu* vcpu)
{
    return vcpu->arch.started;
}

unsigned long vcpu_readreg(struct vcpu* vcpu, unsigned long reg)
{
    if (reg > 32) {
        ERROR("reading register out of bounds");
    }

    /* r0 is always 0x0 */
    return reg == 0 ? 0 : vcpu->regs.gp_regs.r[reg];
}

void vcpu_writereg(struct vcpu* vcpu, unsigned long reg, unsigned long val)
{
    if (reg > 32) {
        ERROR("writing register out of bounds");
    }

    /* r0 is always 0x0 */
    if (reg != 0){
        vcpu->regs.gp_regs.r[reg] = val;
    }
}

unsigned long vcpu_readpc(struct vcpu* vcpu)
{
    return vcpu->regs.pc;
}

void vcpu_writepc(struct vcpu* vcpu, unsigned long val)
{
    vcpu->regs.pc = val;
}

bool vbootctrl_emul_handler(struct emul_access* acc)
{
    struct vcpu* vcpu = cpu()->vcpu;
    struct vm* vm = vcpu->vm;
    unsigned long notify = 0;

    /* Translate access */
    if (acc->arch.op != EMUL_ARCH_BWOP_NO) {
        /* this access is fairly unique, so it's not practical to put behind
         * arch emul */
        size_t virt_id = INVALID_CPUID;

        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            if ((1U << i) & acc->arch.byte_mask) {
                virt_id = vm->vcpus[i].id;
                if (!vm->vcpus[i].arch.started) {
                    notify |= (1UL << vm->vcpus[i].phys_id);
                }
                break;
            }
        }

        if (virt_id != INVALID_CPUID) {
            unsigned long psw = srs_gmpsw_read();
            if (vm->vcpus[virt_id].arch.started) {
                srs_gmpsw_write(psw & ~PSW_Z);
            } else {
                srs_gmpsw_write(psw | PSW_Z);
            }

            switch (acc->arch.op) {
                case EMUL_ARCH_BWOP_SET1:
                    vm->vcpus[virt_id].arch.started = true;
                    break;
                case EMUL_ARCH_BWOP_NOT1:
                    vm->vcpus[virt_id].arch.started = true;
                    break;
                    /* CLR1 accesses are ignored */
                    /* TST1 only modifies the PSW.Z flag */
                default:
                    break;
            }
        }
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            if ((1U << i) & val) {
                if (!vm->vcpus[i].arch.started) {
                    notify |= 1UL << vm->vcpus[i].phys_id;
                }
                vm->vcpus[i].arch.started = true;
            }
        }
    } else {
        unsigned long val = 0;
        for (size_t i = 0; i < vcpu->vm->cpu_num; i++) {
            if (vm->vcpus[i].arch.started) {
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
            .size = 0x10,
            .handler = vbootctrl_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.bootctrl_emul);
    }
}
