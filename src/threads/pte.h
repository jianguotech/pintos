#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* 与 x86 硬件页表相关的函数与宏。

   更多关于虚拟地址的通用函数与宏见 vaddr.h。

   虚拟地址的位域布局如下：

    31                  22 21                  12 11                   0
   +----------------------+----------------------+----------------------+
   | 页目录索引 (PDI)     | 页表索引 (PTI)       | 页内偏移 (Offset)    |
   +----------------------+----------------------+----------------------+
*/

/* Page table index (bits 12:21). */
#define PTSHIFT PGBITS                  /* First page table bit. */
#define PTBITS 10                       /* Number of page table bits. */
#define PTSPAN (1 << PTBITS << PGBITS)  /* Bytes covered by a page table. */
#define PTMASK BITMASK(PTSHIFT, PTBITS) /* Page table bits (12:21). */

/* Page directory index (bits 22:31). */
#define PDSHIFT (PTSHIFT + PTBITS)      /* First page directory bit. */
#define PDBITS 10                       /* Number of page dir bits. */
#define PDMASK BITMASK(PDSHIFT, PDBITS) /* Page directory bits (22:31). */

/* 函数：pt_no
   功能：从虚拟地址提取页表索引
   参数：va - 虚拟地址指针
   返回：页表索引 */
static inline unsigned pt_no(const void *va)
{
    return ((uintptr_t) va & PTMASK) >> PTSHIFT;
}

/* 函数：pd_no
   功能：从虚拟地址提取页目录索引
   参数：va - 虚拟地址指针
   返回：页目录索引 */
static inline uintptr_t pd_no(const void *va)
{
    return (uintptr_t) va >> PDSHIFT;
}

/* 页目录项（PDE）与页表项（PTE）。

   更多信息参考 Pintos 参考手册或 IA32 手册中关于
   "Page-Directory and Page-Table Entries" 的章节。

   PDE 与 PTE 采用相同的格式：高位存放物理地址，低位存放标志位。

   在 PDE 中，物理地址指向页表；在 PTE 中，物理地址指向数据/代码页。
   下面列出的标志位为关键位。当 PDE 或 PTE 的 present 位为 0 时
   其它标志位将被忽略。将 PDE/PTE 初始化为 0 即表示该项"不存在"。 */
#define PTE_FLAGS 0x00000fff /* Flag bits. */
#define PTE_ADDR 0xfffff000  /* Address bits. */
#define PTE_AVL 0x00000e00   /* Bits available for OS use. */
#define PTE_P 0x1            /* 1=present, 0=not present. */
#define PTE_W 0x2            /* 1=read/write, 0=read-only. */
#define PTE_U 0x4            /* 1=user/kernel, 0=kernel only. */
#define PTE_A 0x20           /* 1=accessed, 0=not acccessed. */
#define PTE_D 0x40           /* 1=dirty, 0=not dirty (PTEs only). */

/* 函数：pde_create
   功能：构造一个指向给定页表的 PDE
   参数：pt - 页表的内核虚拟地址
   返回：构造的页目录项（PDE） */
static inline uint32_t pde_create(uint32_t *pt)
{
    ASSERT(pg_ofs(pt) == 0);
    return vtop(pt) | PTE_U | PTE_P | PTE_W;
}

/* 函数：pde_get_pt
   功能：从 PDE 中解析出对应页表的内核虚拟地址
   参数：pde - 页目录项
   返回：页表的内核虚拟地址 */
static inline uint32_t *pde_get_pt(uint32_t pde)
{
    ASSERT(pde & PTE_P);
    return ptov(pde & PTE_ADDR);
}

/* 函数：pte_create_kernel
   功能：构造指向内核页的 PTE，可配置写权限
   参数：page - 页的内核虚拟地址，writable - 是否可写
   返回：构造的页表项（PTE） */
static inline uint32_t pte_create_kernel(void *page, bool writable)
{
    ASSERT(pg_ofs(page) == 0);
    return vtop(page) | PTE_P | (writable ? PTE_W : 0);
}

/* 函数：pte_create_user
   功能：构造同时允许用户访问的 PTE，可配置写权限
   参数：page - 页的内核虚拟地址，writable - 是否可写
   返回：构造的页表项（PTE） */
static inline uint32_t pte_create_user(void *page, bool writable)
{
    return pte_create_kernel(page, writable) | PTE_U;
}

/* 函数：pte_get_page
   功能：从 PTE 中解析出对应物理页的内核虚拟地址
   参数：pte - 页表项
   返回：对应物理页的内核虚拟地址 */
static inline void *pte_get_page(uint32_t pte)
{
    return ptov(pte & PTE_ADDR);
}

#endif /* threads/pte.h */
