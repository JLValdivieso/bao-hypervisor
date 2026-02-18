/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <emul.h>
#include <interrupts.h>
#include <platform.h>
#include <vm.h>
#include <arch/fences.h>
#include <intc.h>
#include <ipir.h>
#include <srs.h>

extern volatile struct intc1* intc1_hw;
extern volatile struct intc2* intc2_hw;
extern volatile struct intif* intif_hw;
extern volatile struct eint* eint_hw;
extern volatile struct fenc* fenc_hw;
extern volatile struct feinc* feinc_hw[PLAT_CPU_NUM];

void vintc_inject(struct vcpu* vcpu, irqid_t int_id)
{
    struct vm* vm = vcpu->vm;

    if (!vm_has_interrupt(vm, int_id)) {
        ERROR("VM tried to access unassigned interrupt");
    }
    intc_set_pend(int_id, true);
}

static inline bool is_addr_aligned_to_width(unsigned long addr, unsigned long width_in_bits)
{
    /* divide width by 8 to get bytes. subtract 1 to get aligment mask */
    unsigned long mask = ((width_in_bits / 8)-1);
    /* if addr & mask is non-zero access is unaligned */
    return (addr & mask) == 0;
}

static void emulate_intc_eic_access(struct emul_access* acc, size_t reg_idx, unsigned long mask)
{
    struct vcpu* vcpu = cpu()->vcpu;
    struct vm* vm = vcpu->vm;

    size_t addr_off = acc->addr & 0x1UL;
    volatile uint16_t* tgt_reg = &(intc2_hw->EIC[reg_idx]);
    irqid_t int_id = reg_idx + 32;

    if (!vm_has_interrupt(vm, int_id)) {
        ERROR("VM tried to access unassigned interrupt");
    }

    if (emul_arch_is_bwop(&acc->arch)) {
        volatile uint8_t* byte_addr = ((volatile uint8_t*)tgt_reg) + addr_off;
        *byte_addr = emul_arch_bwop_emul_acc(&acc->arch, *byte_addr);
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        *tgt_reg =
            (uint16_t)(((val & mask) << (addr_off * 8)) | (*tgt_reg & ~(mask << (addr_off * 8))));
    } else {
        unsigned long val = *tgt_reg;

        val = (val >> (addr_off * 8)) & mask;
        if (acc->sign_ext && (1UL << (acc->width - 1) & val)) {
            val |= ~mask;
        }

        vcpu_writereg(vcpu, acc->reg, val);
    }
}

static void emulate_intc_imr_access(struct emul_access* acc, size_t reg_idx, uint32_t mask)
{
    struct vcpu* vcpu = cpu()->vcpu;
    struct vm* vm = vcpu->vm;
    size_t addr_off = acc->addr & 0x3UL;
    irqid_t first_imr_int = 0;
    volatile uint32_t* tgt_reg = &(intc2_hw->IMR[reg_idx]);

    first_imr_int = reg_idx * 32;
    first_imr_int += 32;

    if (emul_arch_is_bwop(&acc->arch)) {
        volatile uint8_t* byte_addr = ((volatile uint8_t*)tgt_reg) + addr_off;
        *byte_addr = emul_arch_bwop_emul_acc(&acc->arch, *byte_addr);
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        unsigned long write_val = *tgt_reg;

        for (unsigned int i = 0; i < 32; i++) {
            if (!vm_has_interrupt(vm, i + first_imr_int)) {
                continue;
            }
            if ((1UL << i) & val) {
                write_val |= (1UL << i);
            } else {
                write_val &= ~(1UL << i);
            }
        }
        *tgt_reg = ((write_val & mask) << (addr_off * 8)) | (*tgt_reg & ~(mask << (addr_off * 8)));
    } else {
        unsigned long val = 0;

        for (unsigned int i = 0; i < 32; i++) {
            if (!vm_has_interrupt(vm, i + first_imr_int)) {
                continue;
            }
            unsigned int imr_val = *tgt_reg;
            if ((1UL << i) & imr_val) {
                val |= (1UL << i);
            }
        }

        val = (val >> (addr_off * 8)) & mask;
        if (acc->sign_ext && (1UL << (acc->width - 1) & val)) {
            val |= ~mask;
        }
        vcpu_writereg(vcpu, acc->reg, val);
    }
}

static void emulate_intc_eibd_access(struct emul_access* acc, size_t reg_idx, uint32_t mask)
{
    struct vcpu* vcpu = cpu()->vcpu;
    struct vm* vm = vcpu->vm;

    size_t addr_off = acc->addr & 0x3UL;
    irqid_t int_id = 0;
    volatile uint32_t* tgt_reg = &(intc2_hw->EIBD[reg_idx]);
    int_id = reg_idx + 32;

    if (!vm_has_interrupt(vm, int_id)) {
        ERROR("VM tried to access unassigned interrupt");
    }

    if (emul_arch_is_bwop(&acc->arch)) {
        volatile uint8_t* byte_addr = ((volatile uint8_t*)tgt_reg) + addr_off;
        *byte_addr = emul_arch_bwop_emul_acc(&acc->arch, *byte_addr);
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        unsigned long virt_peid = val & 0x7UL;
        unsigned long phys_peid = vm_translate_to_pcpuid(vm, virt_peid);
        if (phys_peid != INVALID_CPUID) {
            val = (val & 0xFFFF0000) | (*tgt_reg & 0xFFF8) | (phys_peid & 0x7UL);
        } else {
            val = (val & 0xFFFF0000) | (*tgt_reg & ~0xFFFF0000);
        }
        *tgt_reg = ((val & mask) << (addr_off * 8)) | (*tgt_reg & ~(mask << (addr_off * 8)));
    } else {
        unsigned long val = *tgt_reg;
        unsigned long phys_peid = val & 0x7UL;
        unsigned long virt_peid = INVALID_CPUID;
        for (size_t i = 0; i < vm->cpu_num; i++) {
            struct vcpu* vcpu_trgt = vm_get_vcpu(vcpu->vm, i);
            if(vcpu_trgt == NULL){
                continue;
            }
            if (vcpu_trgt->phys_id == phys_peid) {
                virt_peid = vcpu_trgt->id;
                break;
            }
        }
        if (virt_peid != INVALID_CPUID) {
            val = (val & 0xFFFF0000) | (virt_peid & 0x7UL);
        } else {
            val = (val & 0xFFFF0000);
        }

        val = (val >> (addr_off * 8)) & mask;
        if (acc->sign_ext && (1UL << (acc->width - 1) & val)) {
            val |= ~mask;
        }

        vcpu_writereg(vcpu, acc->reg, val);
    }
}

static void emulate_intc_eeic_access(struct emul_access* acc, size_t reg_idx, uint32_t mask)
{
    struct vcpu* vcpu = cpu()->vcpu;
    struct vm* vm = vcpu->vm;

    size_t addr_off = acc->addr & 0x3UL;
    volatile uint32_t* tgt_reg = &(intc2_hw->EEIC[reg_idx]);
    irqid_t int_id = reg_idx + 32;

    if (!vm_has_interrupt(vm, int_id)) {
        ERROR("VM tried to access unassigned interrupt");
    }

    if (emul_arch_is_bwop(&acc->arch)) {
        volatile uint8_t* byte_addr = ((volatile uint8_t*)tgt_reg) + addr_off;
        *byte_addr = emul_arch_bwop_emul_acc(&acc->arch, *byte_addr);
    } else if (acc->write) {
        unsigned long val = vcpu_readreg(vcpu, acc->reg);
        *tgt_reg = ((val & mask) << (addr_off * 8)) | (*tgt_reg & ~(mask << (addr_off * 8)));
    } else {
        unsigned long val = *tgt_reg;

        val = (val >> (addr_off * 8)) & mask;
        if (acc->sign_ext && (1UL << (acc->width - 1) & val)) {
            val |= ~mask;
        }

        vcpu_writereg(vcpu, acc->reg, val);
    }
}

static unsigned long width_to_mask(unsigned long width)
{
    unsigned long mask = 0;
    if(width == 8) {
        mask = 0xFFUL;
    } else if(width == 16) {
        mask = 0xFFFFUL;
    } else if (width == 32) {
        mask = 0xFFFFFFFFUL;
    }
    return mask;
}

static bool vintc2_emul_handler(struct emul_access* acc)
{
    if (acc->width > 32) {
        return false;
    }

    if(!is_addr_aligned_to_width(acc->addr, acc->width)){
        /* Access addr is not aligned to access width */
        return false;
    }

    size_t acc_offset = acc->addr - platform.arch.intc.intc2_addr;
    unsigned long mask = width_to_mask(acc->width);

    size_t intc2_eic_bot = offsetof(struct intc2, EIC);
    size_t intc2_eic_top = sizeof(((struct intc2*)NULL)->EIC) + intc2_eic_bot;
    size_t intc2_eic_idx = (ALIGN(acc_offset - intc2_eic_bot, 2)) / 2;
    if (acc_offset >= intc2_eic_bot && acc_offset < intc2_eic_top) {
        emulate_intc_eic_access(acc, intc2_eic_idx, mask);
        return true;
    }

    size_t intc2_imr_bot = offsetof(struct intc2, IMR);
    size_t intc2_imr_top = sizeof(((struct intc2*)NULL)->IMR) + intc2_imr_bot;
    size_t intc2_imr_idx = (ALIGN(acc_offset - intc2_imr_bot, 4)) / 4;
    if (acc_offset >= intc2_imr_bot && acc_offset < intc2_imr_top) {
        emulate_intc_imr_access(acc, intc2_imr_idx, mask);
        return true;
    }

    size_t intc2_eibd_bot = offsetof(struct intc2, EIBD);
    size_t intc2_eibd_top = sizeof(((struct intc2*)NULL)->EIBD) + intc2_eibd_bot;
    size_t intc2_eibd_idx = (ALIGN(acc_offset - intc2_eibd_bot, 4)) / 4;
    if (acc_offset >= intc2_eibd_bot && acc_offset < intc2_eibd_top) {
        emulate_intc_eibd_access(acc, intc2_eibd_idx, mask);
        return true;
    }

    size_t intc2_eeic_bot = offsetof(struct intc2, EEIC);
    size_t intc2_eeic_top = sizeof(((struct intc2*)NULL)->EEIC) + intc2_eeic_bot;
    size_t intc2_eeic_idx = (ALIGN(acc_offset - intc2_eeic_bot, 4)) / 4;
    if (acc_offset >= intc2_eeic_bot && acc_offset < intc2_eeic_top) {
        emulate_intc_eeic_access(acc, intc2_eeic_idx, mask);
        return true;
    }

    /* Ignore access */
    if (!acc->write && acc->arch.bwop == EMUL_ARCH_BWOP_NO) {
        vcpu_writereg(cpu()->vcpu, acc->reg, 0);
    }

    return true;
}

static bool vintif_emul_handler(struct emul_access* acc)
{
    UNUSED_ARG(acc);
    ERROR("%s not implemented", __func__);
    return false;
}

static bool veint_emul_handler(struct emul_access* acc)
{
    UNUSED_ARG(acc);
    ERROR("%s not implemented", __func__);
    return false;
}

static bool vfenc_emul_handler(struct emul_access* acc)
{
    UNUSED_ARG(acc);
    ERROR("%s not implemented", __func__);
    return false;
}

static bool vfeinc_emul_handler(struct emul_access* acc)
{
    UNUSED_ARG(acc);
    ERROR("%s not implemented", __func__);
    return false;
}

void vintc_init(struct vm* vm)
{
    if (cpu()->id == vm->master) {
        vm->arch.intc2_emul = (struct emul_mem){
            .va_base = platform.arch.intc.intc2_addr,
            .size = ALIGN(sizeof(struct intc2), PAGE_SIZE),
            .handler = vintc2_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.intc2_emul);

        /* of the following which can we bypass? */
        vm->arch.intif_emul = (struct emul_mem){
            .va_base = platform.arch.intc.intif_addr,
            .size = ALIGN(sizeof(struct intif), PAGE_SIZE),
            .handler = vintif_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.intif_emul);

        vm->arch.eint_emul = (struct emul_mem){
            .va_base = platform.arch.intc.eint_addr,
            .size = ALIGN(sizeof(struct eint), PAGE_SIZE),
            .handler = veint_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.eint_emul);

        vm->arch.fenc_emul = (struct emul_mem){
            .va_base = platform.arch.intc.fenc_addr,
            .size = ALIGN(sizeof(struct fenc), PAGE_SIZE),
            .handler = vfenc_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.fenc_emul);

        vm->arch.feinc_emul = (struct emul_mem){
            .va_base = platform.arch.intc.feinc_addr[cpu()->id],
            .size = ALIGN(sizeof(struct feinc), PAGE_SIZE),
            .handler = vfeinc_emul_handler,
        };
        vm_emul_add_mem(vm, &vm->arch.feinc_emul);

        // TODO: Add spinlock for INTC emulation
    }
}

void vintc_vcpu_reset(struct vcpu* vcpu)
{
    for (size_t i = 0; i < PRIVATE_IRQS_NUM; i++) {
        if (vm_has_interrupt(vcpu->vm, i)) {
            intc_set_trgt(i, vcpu->phys_id);
            intc_set_enable(i, false);
            intc_set_prio(i, 0);
            intc_set_pend(i, false);
        }
    }
}

void vintc_vm_reset(struct vm* vm)
{
    if (vm->master == cpu()->id) {
        for (size_t i = PRIVATE_IRQS_NUM; i < PLAT_MAX_INTERRUPTS; i++) {
            if (vm_has_interrupt(vm, i)) {
                intc_set_trgt(i, cpu()->id);
                intc_set_enable(i, false);
                intc_set_prio(i, 0);
                intc_set_pend(i, false);
            }
        }
    }
}
