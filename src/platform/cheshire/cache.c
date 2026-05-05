/**
 * SPDX-License-Identifier: Apache-2.0
 * Cache flush for Cheshire (CVA6)
 */

#include <cache.h>
#include <fences.h>

void cache_flush_range(vaddr_t base, size_t size)
{
    UNUSED_ARG(base);
    UNUSED_ARG(size);

    fence_sync();
    fencei();
}