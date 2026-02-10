/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <emul.h>
#include <hypercall.h>
#include <fences.h>
#include <vmm.h>
#include <arch/aborts.h>
#include <arch/emul.h>
#include <srs.h>

#define MDP_HOST (0x91)
#define MDP_GUEST (0x99)
#define HVTRAP_LOW (0xf000)
#define HVTRAP_HIGH (0xf01f)

#define F8_OPCODE        (0x3EUL)
#define F9_OPCODE        (0x3FUL)
#define F9_SUBOPCODE     (0x1CUL)

#define OPCODE_SHIFT     (5)
#define OPCODE_MASK      (0x3FUL << OPCODE_SHIFT)

#define SUBOPCODE_SHIFT  (19)
#define SUBOPCODE_MASK   (0x1FFFUL << SUBOPCODE_SHIFT)

#define SUB8_SHIFT       (14)
#define SUB8_MASK        (0x3UL << SUB8_SHIFT)

#define SUB9_SHIFT       (17)
#define SUB9_MASK        (0x3UL << SUB9_SHIFT)

#define BITIDX_SHIFT     (11)
#define BITIDX_MASK      (0x7UL << BITIDX_SHIFT)

#define REGIDX_SHIFT     (11)
#define REGIDX_MASK      (0x1FUL << REGIDX_SHIFT)

// LEN (Bits 31-28)
#define MEI_LEN_MASK     (0xFUL << 28)
#define MEI_LEN_SHIFT    28
#define MEI_GET_LEN(val) (((val) & MEI_LEN_MASK) >> MEI_LEN_SHIFT)

// REG (Bits 20-16)
#define MEI_REG_MASK     (0x1F << 16)
#define MEI_REG_SHIFT    16
#define MEI_GET_REG(val) (((val) & MEI_REG_MASK) >> MEI_REG_SHIFT)

// DS (Bits 11-9)
#define MEI_DS_MASK      (0x7 << 9)
#define MEI_DS_SHIFT     9
#define MEI_GET_DS(val)  (((val) & MEI_DS_MASK) >> MEI_DS_SHIFT)

// U (Bit 8)
#define MEI_U_MASK       (1 << 8)
#define MEI_GET_U(val)   (((val) & MEI_U_MASK) >> 8)

// RW (Bit 0)
#define MEI_RW_MASK      (1 << 0)
#define MEI_GET_RW(val)  ((val) & MEI_RW_MASK)

static unsigned long read_instruction(unsigned long pc)
{
    unsigned long inst = 0;
    unsigned short* pc_ptr = (unsigned short*)(pc);

    if (pc & 0x1) {
        ERROR("Trying to read guest unaligned instruction");
    }

    /* Enable Hyp access to VM space */
    srs_mpid7_write(HYP_SPID);
    fence_sync_write();

    inst = (unsigned long)(*pc_ptr | (*(pc_ptr + 1) << 16));

    /* Disable Hyp access to VM space */
    srs_mpid7_write(HYP_AUX_SPID);
    fence_sync();

    return inst;
}

static void data_abort(void)
{
    unsigned long mea = srs_mea_read();
    unsigned long mei = srs_mei_read();

    unsigned int len = MEI_GET_LEN(mei);
    unsigned int reg = MEI_GET_REG(mei);
    unsigned int ds = MEI_GET_DS(mei);
    unsigned int u = MEI_GET_U(mei);
    unsigned int rw = MEI_GET_RW(mei);
    vaddr_t addr = mea;

    /* Decode possible bitwise instruction */
    unsigned long inst = read_instruction(vcpu_readpc(cpu()->vcpu));
    unsigned long opcode = ((inst & OPCODE_MASK) >> OPCODE_SHIFT);
    unsigned long subopcode = ((inst & SUBOPCODE_MASK) >> SUBOPCODE_SHIFT);
    unsigned long bit_op = 0;
    unsigned long mask = 0;

    if (opcode == F8_OPCODE) {
        mask = 1UL << ((inst & BITIDX_MASK) >> BITIDX_SHIFT);
        bit_op = ((inst & SUB8_MASK) >> SUB8_SHIFT) + 1;
    } else if (opcode == F9_OPCODE && subopcode == F9_SUBOPCODE) {
        unsigned long reg_idx = (inst & REGIDX_MASK) >> REGIDX_SHIFT;
        unsigned long bit_idx = vcpu_readreg(cpu()->vcpu, reg_idx);
        mask = 1UL << (bit_idx & 0x7UL);
        bit_op = ((inst & SUB9_MASK) >> SUB9_SHIFT) + 1;
    }

    emul_handler_t handler = vm_emul_get_mem(cpu()->vcpu->vm, addr);
    if (handler != NULL) {
        struct emul_access emul;
        emul.addr = addr;
        emul.width = len;
        emul.write = rw ? true : false;
        emul.reg = reg;
        emul.reg_width = ds;
        emul.sign_ext = ~u;

        emul.arch.op = (enum emul_arch_bwop)bit_op;
        emul.arch.byte_mask = mask;

        if (handler(&emul)) {
            unsigned long pc_step = len;
            vcpu_writepc(cpu()->vcpu, vcpu_readpc(cpu()->vcpu) + pc_step);
        } else {
            ERROR("Data abort emulation failed (0x%x)", addr);
        }
    } else {
        ERROR("No emulation handler for access to 0x%x, at 0x%x", addr, vcpu_readpc(cpu()->vcpu));
    }
}

static void hvtrap(void)
{
    unsigned long r6 = vcpu_readreg(cpu()->vcpu, 6);
    unsigned long res = (unsigned long)hypercall(r6);
    vcpu_writereg(cpu()->vcpu, 6, res);
}

void abort(void)
{
    unsigned long psw = srs_psw_read();
    unsigned long cause = (psw & (0x1UL << 7)) ? (srs_feic_read() & 0xFFFFUL) : (srs_eiic_read() & 0xFFFFUL);

    switch (cause) {
        case MDP_GUEST:
            data_abort();
            break;
        case MDP_HOST:
            ERROR("Host data abort");
            break;
        default:
            if (cause >= HVTRAP_LOW && cause <= HVTRAP_HIGH) {
                hvtrap();
            } else {
                WARNING("Exception not handled. Cause: 0x%lx", cause);
            }
    }
}
