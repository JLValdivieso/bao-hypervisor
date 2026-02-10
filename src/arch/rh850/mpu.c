/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <bao.h>
#include <cpu.h>
#include <arch/mpu.h>
#include <srs.h>
#include <arch/fences.h>

static inline size_t mpu_num_entries(void)
{
    unsigned long mpcfg = srs_mpcfg_read();
    size_t num = (mpcfg & 0x1f) + 1;
    return num;
}

static inline void mpu_lock_entry(mpid_t mpid)
{
    bitmap_set(cpu()->arch.mpu_hyp.locked, mpid);
}

static void mpu_entry_set(mpid_t mpid, struct mp_region* mpr)
{
    unsigned long lim = mpr->base + mpr->size - 4;

    srs_mpidx_write(mpid & MPIDX_IDX_MASK);
    srs_mpla_write(mpr->base & MPLA_MASK);
    srs_mpua_write(lim & MPUA_MASK);
    srs_mpat_write(mpr->mem_flags.raw);

    syncp();
}

static void mpu_entry_clear(mpid_t mpid)
{
    srs_mpidx_write(mpid & MPIDX_IDX_MASK);
    srs_mpla_write(0);
    srs_mpua_write(0);
    srs_mpat_write(0);

    syncp();
}

static mpid_t mpu_entry_allocate_hyp(void)
{
    mpid_t reg_num = INVALID_MPID;
    for (mpid_t i = (mpid_t)mpu_num_entries(); i > 0; i--) {
        /* mpid_t is unsigned so we can't test for negative number.
         * Subtract 1 to get the index */
        mpid_t idx = i - 1;
        if (bitmap_get(cpu()->arch.mpu_hyp.bitmap, idx) == 0) {
            bitmap_set(cpu()->arch.mpu_hyp.bitmap, idx);
            reg_num = idx;

            break;
        }
    }

    return reg_num;
}

static inline void mpu_set_hbe(unsigned long hbe)
{
    unsigned long mpcfg = srs_mpcfg_read();
    mpcfg = (mpcfg & ~MPCFG_HBE_MASK) | (hbe << MPCFG_HBE_OFF);
    srs_mpcfg_write(mpcfg);
}

bool mpu_add_region(struct mp_region* reg, bool locked)
{
    bool failed = true;

    if (reg->size > 0) {
        mpid_t mpid = 0;
        mpid = mpu_entry_allocate_hyp();

        if (mpid != INVALID_MPID) {
            failed = false;
            mpu_entry_set(mpid, reg);
            if (locked) {
                mpu_lock_entry(mpid);
            }
        }
        mpu_set_hbe(mpid);
    }

    return !failed;
}

static void mpu_entry_get_region(mpid_t mpid, struct mp_region* mpe)
{
    srs_mpidx_write(mpid & MPIDX_IDX_MASK);
    syncp();

    unsigned long base = srs_mpla_read();
    unsigned long limit = srs_mpua_read();

    mpe->mem_flags.raw = srs_mpat_read();
    mpe->base = base;
    mpe->size = (limit - base) + 4;
    mpe->as_sec = SEC_UNKNOWN;
}

static mpid_t mpu_entry_get_region_id(struct mp_region* mpe)
{
    mpid_t mpid = INVALID_MPID;

    for (mpid_t i = 0; i < (mpid_t)mpu_num_entries(); i++) {
        if (bitmap_get(cpu()->arch.mpu_hyp.bitmap, i)) {
            struct mp_region mpe_cmp;
            mpu_entry_get_region(i, &mpe_cmp);

            if (mpe_cmp.base == mpe->base && mpe_cmp.size == mpe->size) {
                mpid = i;
                break;
            }
        }
    }

    return mpid;
}

static inline void mpu_entry_free(mpid_t mpid)
{
    mpu_entry_clear(mpid);
    bitmap_clear(cpu()->arch.mpu_hyp.bitmap, mpid);
}

bool mpu_remove_region(struct mp_region* reg)
{
    bool failed = true;

    if (reg->size > 0) {
        mpid_t mpid = mpu_entry_get_region_id(reg);

        if (mpid != INVALID_MPID) {
            failed = false;
            mpu_entry_free(mpid);
        }
    }

    return !failed;
}

bool mpu_update_region(struct mp_region* mpr)
{
    bool failed = true;

    for (mpid_t mpid = 0; mpid < (mpid_t)mpu_num_entries(); mpid++) {
        if (bitmap_get(cpu()->arch.mpu_hyp.bitmap, mpid) == 0) {
            continue;
        }
        struct mp_region mpe_cmp;
        mpu_entry_get_region(mpid, &mpe_cmp);

        if (mpe_cmp.base == mpr->base) {
            mpu_entry_set(mpid, mpr);
            failed = false;
            break;
        }
    }

    return !failed;
}

static inline bool mpu_entry_valid(mpid_t mpid)
{
    srs_mpidx_write(mpid & MPIDX_IDX_MASK);
    unsigned long attr = srs_mpat_read();
    unsigned long valid_bit = (attr & (1 << 7)) >> 7;

    return !!valid_bit;
}

void mpu_arch_init(void)
{
    bitmap_clear_consecutive(cpu()->arch.mpu_hyp.bitmap, 0, mpu_num_entries());

    for (mpid_t mpid = 0; mpid < (mpid_t)mpu_num_entries(); mpid++) {
        /* No entry should be valid at this point */
        if (mpu_entry_valid(mpid)) {
            bitmap_set(cpu()->arch.mpu_hyp.bitmap, mpid);
            bitmap_set(cpu()->arch.mpu_hyp.locked, mpid);
        }
    }

    /* give no mpu entries to hypervisor, we will allocate them dynamically */
    mpu_set_hbe(mpu_num_entries());

    /* At this point we configure MPIDs as PEID to perform platform initialization */
    unsigned long peid = srs_peid_read();
    srs_mpid6_write(peid);
    srs_spid_write(peid);
}

void mpu_arch_enable(void)
{
    srs_mpm_write(MPM_SVP | MPM_MPE);
}

void mpu_arch_disable(void)
{
    unsigned long mpm = srs_mpm_read() & ~MPM_MPE;
    srs_mpm_write(mpm);
}
