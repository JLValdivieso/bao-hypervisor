/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <cpu.h>
#include <vm.h>
#include <emul.h>
#include <config.h>
#include <arch/traps.h>
#include <arch/csfrs.h>
#include <arch/fences.h>
#include <hypercall.h>
#include <arch/decode.h>

void sys_bus_errors_handler(void)
{
    /* this trap suppresses errors to the first address in a device region.
    This address is often the CLC register that is used to enable the peripheral.
    Since the access to this register is managed by the PROT mechanism, when
    the slave side mmio protection is enable, a guest trying to enable the device
    will always trap and we suppress the error.
    This is trade-off that covers a majority of devices in TC4 and allows for
    most BSPs to execute correctly. */
    unsigned long vmid = cpu()->vcpu->vm->id;

    unsigned long addr = csfr_deadd_read();

    unsigned long ret = 0;

    for (unsigned long i = 0; i < config.vmlist[vmid].platform.dev_num; i++) {
        unsigned long dev_addr = config.vmlist[vmid].platform.devs[i].pa;

        if (dev_addr == addr) {
            ret = 1;
        }
    }

    if (!ret) {
        ERROR("vm accesing a device it doesn't own or trying to acccess a PROT/APU/CLC register");
    }
}

void l2_dmem_prot_trap_handler(unsigned long* instr_addr, unsigned long is_write)
{
    struct emul_access emul;
    /* Give bao the same read permissions on the mpu */
    /* We save the bao prs bitmap, and we OR it with the guest prs */
    volatile unsigned long hyp_d_r_entries = csfr_dpre_0_read();
    unsigned long vmid = cpu()->vcpu->vm->id;

    volatile unsigned long vm_d_r_entries = get_dpre(vmid + 1);

    volatile unsigned long perms = hyp_d_r_entries | vm_d_r_entries;
    set_dpre(0, perms);

    /* We are changing the active entries in the PRS we are using. A fence
    is needed to ensure the upcoming accesses see the correct permissions. */
    fence_sync();

    unsigned long ins = *(unsigned long*)instr_addr;

    unsigned long opcode = bit32_extract(ins, 0, 8);

    volatile bool reg = 0;

    unsigned long addr = csfr_deadd_read();

    emul_handler_t handler = vm_emul_get_mem(cpu()->vcpu->vm, addr);

    if (handler != NULL) {
        // Only adjust the return addr if there is an emul_handler.
        if (opcode % 2 == 0) {
            reg = decode_16b_access(ins, &emul);
            cpu()->vcpu->regs.lower_ctx.a11 += 2;
        } else {
            reg = decode_32b_access(ins, &emul);
            cpu()->vcpu->regs.lower_ctx.a11 += 4;
        }

        if (reg == false) {
            return;
        }

        emul.addr = addr;
        emul.width = emul.reg_width;
        emul.write = !!is_write;
        emul.sign_ext = false;

        handler(&emul);
    }

    set_dpre(0, hyp_d_r_entries);
}

void hvcall_handler(unsigned long function_id)
{
    hypercall(function_id);
}

static void csfr_emul_handler(struct emul_access* emul, unsigned long csfr)
{
    UNUSED_ARG(emul);
    switch (csfr) {
        default:
            WARNING("Function csfr_emul_handler not implemented");
            break;
    }
    return;
}

void hyp_csfr_access_handler(unsigned long* instr_addr, unsigned long hvtin)
{
    struct emul_access emul;
    /* Give bao the same read permissions on the mpu */
    /* We save the bao prs bitmap, and we OR it with the guest prs */
    volatile unsigned long hyp_d_r_entries = csfr_dpre_0_read();
    unsigned long vmid = cpu()->vcpu->vm->id;

    volatile unsigned long vm_d_r_entries = get_dpre(vmid + 1);

    volatile unsigned long perms = hyp_d_r_entries | vm_d_r_entries;
    set_dpre(0, perms);

    fence_sync();

    unsigned long ins = *(unsigned long*)instr_addr;

    decode_cfr_access(ins, &emul);

    csfr_emul_handler(&emul, (hvtin & 0xFFFF));

    cpu()->vcpu->regs.lower_ctx.a11 += 4;

    set_dpre(0, hyp_d_r_entries);
}
