#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include "threads/loader.h"
#include <debug.h>
#include <stdbool.h>
#include <stdint.h>

/* 功能：提供虚拟地址相关的便捷宏与内联函数。
   说明：更多页表相关工具见 pte.h。 */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                       /* Index of first offset bit. */
#define PGBITS 12                       /* Number of offset bits. */
#define PGSIZE (1 << PGBITS)            /* Bytes in a page. */
#define PGMASK BITMASK(PGSHIFT, PGBITS) /* Page offset bits (0:12). */

/* 函数：pg_ofs
   功能：返回虚拟地址在页内的偏移量
   参数：va - 虚拟地址指针
   返回：页内偏移量 */
static inline unsigned pg_ofs(const void *va)
{
    return (uintptr_t) va & PGMASK;
}

/* 函数：pg_no
   功能：返回虚拟地址对应的页号
   参数：va - 虚拟地址指针
   返回：页号 */
static inline uintptr_t pg_no(const void *va)
{
    return (uintptr_t) va >> PGBITS;
}

/* 函数：pg_round_up
   功能：将地址向上对齐到页边界
   参数：va - 虚拟地址指针
   返回：向上对齐后的地址 */
static inline void *pg_round_up(const void *va)
{
    return (void *) (((uintptr_t) va + PGSIZE - 1) & ~PGMASK);
}

/* 函数：pg_round_down
   功能：将地址向下对齐到页边界
   参数：va - 虚拟地址指针
   返回：向下对齐后的地址 */
static inline void *pg_round_down(const void *va)
{
    return (void *) ((uintptr_t) va & ~PGMASK);
}

/* 物理地址到内核虚拟地址的恒等映射起始位置。
   用户程序的地址空间上限亦止于此，以上区域由内核占用。 */
#define PHYS_BASE ((void *) LOADER_PHYS_BASE)

/* 函数：is_user_vaddr
   功能：判断地址是否位于用户空间
   参数：vaddr - 虚拟地址指针
   返回：true - 位于用户空间，false - 位于内核空间 */
static inline bool
is_user_vaddr(const void *vaddr)
{
    return vaddr < PHYS_BASE;
}

/* 函数：is_kernel_vaddr
   功能：判断地址是否位于内核空间
   参数：vaddr - 虚拟地址指针
   返回：true - 位于内核空间，false - 位于用户空间 */
static inline bool
is_kernel_vaddr(const void *vaddr)
{
    return vaddr >= PHYS_BASE;
}

/* 函数：ptov
   功能：将物理地址转换为内核虚拟地址
   参数：paddr - 物理地址
   返回：对应的内核虚拟地址 */
static inline void *
ptov(uintptr_t paddr)
{
    ASSERT((void *) paddr < PHYS_BASE);

    return (void *) (paddr + PHYS_BASE);
}

/* 函数：vtop
   功能：将内核虚拟地址转换为对应物理地址
   参数：vaddr - 内核虚拟地址指针
   返回：对应的物理地址 */
static inline uintptr_t
vtop(const void *vaddr)
{
    ASSERT(is_kernel_vaddr(vaddr));

    return (uintptr_t) vaddr - (uintptr_t) PHYS_BASE;
}

#endif /* threads/vaddr.h */
