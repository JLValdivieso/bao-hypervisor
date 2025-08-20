/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) Bao Project and Contributors. All rights reserved.
 */

#include <bao.h>
#include <cpu.h>
#include <platform.h>
#include <arch/csfrs.h>
#include <arch/prot.h>
#include <arch/csa.h>

cpuid_t CPU_MASTER __attribute__((section(".data")));

static inline void cpu_reset_csa(void)
{
    unsigned long core_id = csfr_coreid_read();
    unsigned long old_fcx = csfr_fcx_read();
    unsigned long old_pcxi = csfr_pcxi_read();

    core_id &= 0x7;

    csfr_fcx_write(old_pcxi);

    union csa* entry = &csa_array[core_id][1];
    entry->lower.pcxi = old_fcx;
}

static inline void cpu_reset_stack(void)
{
    unsigned long stack_top = (unsigned long)&(cpu()->stack[STACK_SIZE]);

    __asm__ volatile("mov.a %%sp, %0" : : "d"(stack_top) : "memory");
}

/* Perform architecture dependent cpu cores initializations */
void cpu_arch_init(cpuid_t cpuid, paddr_t load_addr)
{
    if (cpuid == CPU_MASTER) {
        platform_cpu_init(cpuid, load_addr);
    }
}

void cpu_arch_powerdown(void)
{
    __asm__ volatile("wait \n\r" ::: "memory");

    cpu_reset_csa();
    cpu_reset_stack();

    __asm__ volatile("j cpu_powerdown_wakeup");

    ERROR("returned from powerdown wake up");
}

void cpu_arch_standby(void)
{
    __asm__ volatile("wait \n\r" ::: "memory");

    cpu_reset_csa();
    cpu_reset_stack();

    __asm__ volatile("j cpu_standby_wakeup");

    ERROR("returned from standby wake up");
}

/* The current GCC injects calls to abort() in some places, which
   results in undefined references.
   For this reason we define our own abort() for this architecture. */
void abort(void)
{
    ERROR("abort() reached!");
}
