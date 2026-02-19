#include <platform.h>
#include <interrupts.h>

struct platform platform = {

    .cpu_num = 2,

    .region_num = 1,
    .regions =  (struct mem_region[]) {
        {
            .base = 0x80200000,
            .size = 0x40000000 - 0x200000
        }
    },

    .console = {
        .base = 0x03002000,
    },

    .arch = {
        #if (IRQC == PLIC)
        .irqc.plic.base = 0x04000000,        
        #elif (IRQC == APLIC)
        .irqc.aia.aplic.base = 0x00000000,
        #else 
        #error "unknown IRQC type " IRQC
        #endif       
    }
};
