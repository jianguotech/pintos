/* 文件：pagedir.c
   功能：实现x86架构的页目录管理
   描述：本文件实现了页式虚拟内存管理，包括页目录和页表的创建、销毁和操作
         支持虚拟地址到物理地址的映射、页权限控制和访问位管理 */

#include "userprog/pagedir.h"

#include "threads/init.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* 内部辅助函数声明
   功能：用于获取当前活动的页目录和使页目录失效 */
static uint32_t *active_pd(void);
static void invalidate_pagedir(uint32_t *);

/* 函数：pagedir_create
   功能：创建一个新的页目录，其中包含内核虚拟地址的映射，但不包含用户虚拟地址的映射
         返回新的页目录，如果内存分配失败则返回空指针
   参数：无
   返回：新创建的页目录指针，失败返回NULL */
uint32_t *
pagedir_create(void)
{
    uint32_t *pd = palloc_get_page(0);
    if (pd != NULL)
        memcpy(pd, init_page_dir, PGSIZE);
    return pd;
}

/* 函数：pagedir_destroy
   功能：销毁页目录PD，释放它引用的所有页面
   参数：pd - 要销毁的页目录指针
   返回：无 */
void pagedir_destroy(uint32_t *pd)
{
    uint32_t *pde;

    if (pd == NULL)
        return;

    ASSERT(pd != init_page_dir);
    for (pde = pd; pde < pd + pd_no(PHYS_BASE); pde++)
        if (*pde & PTE_P) {
            uint32_t *pt = pde_get_pt(*pde);
            uint32_t *pte;

            for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++)
                if (*pte & PTE_P)
                    palloc_free_page(pte_get_page(*pte));
            palloc_free_page(pt);
        }
    palloc_free_page(pd);
}

/* 函数：lookup_page
   功能：返回页目录PD中虚拟地址VADDR对应的页表项地址
   描述：如果PD没有VADDR的页表，行为取决于CREATE参数
         如果CREATE为true，则创建一个新页表并返回指向它的指针
         否则，返回空指针
   参数：pd - 页目录指针
         vaddr - 虚拟地址
         create - 是否创建新页表
   返回：页表项指针，失败返回NULL */
static uint32_t *
lookup_page(uint32_t *pd, const void *vaddr, bool create)
{
    uint32_t *pt, *pde;

    ASSERT(pd != NULL);

    /* 不应创建新的内核虚拟映射 */
    ASSERT(!create || is_user_vaddr(vaddr));

    /* 检查VADDR的页表
       如果缺失，根据请求创建一个 */
    pde = pd + pd_no(vaddr);
    if (*pde == 0) {
        if (create) {
            pt = palloc_get_page(PAL_ZERO);
            if (pt == NULL)
                return NULL;

            *pde = pde_create(pt);
        }
        else
            return NULL;
    }

    /* 返回页表项 */
    pt = pde_get_pt(*pde);
    return &pt[pt_no(vaddr)];
}

/* 函数：pagedir_set_page
   功能：在页目录PD中添加从用户虚拟页UPAGE到由内核虚拟地址KPAGE标识的物理帧的映射
   描述：UPAGE必须尚未映射
         KPAGE应该是通过palloc_get_page()从用户池获得的页
         如果WRITABLE为true，新页可读/写；否则为只读
   参数：pd - 页目录指针
         upage - 用户虚拟页地址
         kpage - 内核虚拟页地址（物理帧）
         writable - 是否可写
   返回：成功返回true，内存分配失败返回false */
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool writable)
{
    uint32_t *pte;

    ASSERT(pg_ofs(upage) == 0);
    ASSERT(pg_ofs(kpage) == 0);
    ASSERT(is_user_vaddr(upage));
    ASSERT(vtop(kpage) >> PTSHIFT < init_ram_pages);
    ASSERT(pd != init_page_dir);

    pte = lookup_page(pd, upage, true);

    if (pte != NULL) {
        ASSERT((*pte & PTE_P) == 0);
        *pte = pte_create_user(kpage, writable);
        return true;
    }
    else
        return false;
}

/* 函数：pagedir_get_page
   功能：查找PD中用户虚拟地址UADDR对应的物理地址
   描述：返回与该物理地址对应的内核虚拟地址，如果UADDR未映射则返回空指针
   参数：pd - 页目录指针
         uaddr - 用户虚拟地址
   返回：对应的内核虚拟地址，未映射返回NULL */
void *
pagedir_get_page(uint32_t *pd, const void *uaddr)
{
    uint32_t *pte;

    ASSERT(is_user_vaddr(uaddr));

    pte = lookup_page(pd, uaddr, false);
    if (pte != NULL && (*pte & PTE_P) != 0)
        return pte_get_page(*pte) + pg_ofs(uaddr);
    else
        return NULL;
}

/* 函数：pagedir_clear_page
   功能：在页目录PD中将用户虚拟页UPAGE标记为"不存在"
   描述：后续对该页的访问将产生错误
         页表项中的其他位被保留
         UPAGE不必已被映射
   参数：pd - 页目录指针
         upage - 用户虚拟页地址
   返回：无 */
void pagedir_clear_page(uint32_t *pd, void *upage)
{
    uint32_t *pte;

    ASSERT(pg_ofs(upage) == 0);
    ASSERT(is_user_vaddr(upage));

    pte = lookup_page(pd, upage, false);
    if (pte != NULL && (*pte & PTE_P) != 0) {
        *pte &= ~PTE_P;
        invalidate_pagedir(pd);
    }
}

/* 函数：pagedir_is_dirty
   功能：检查PD中虚拟页VPAGE的页表项是否是脏的
   描述：即自安装PTE以来页面是否被修改过
         如果PD不包含VPAGE的PTE，则返回false
   参数：pd - 页目录指针
         vpage - 虚拟页地址
   返回：是脏的返回true，否则返回false */
bool pagedir_is_dirty(uint32_t *pd, const void *vpage)
{
    uint32_t *pte = lookup_page(pd, vpage, false);
    return pte != NULL && (*pte & PTE_D) != 0;
}

/* 函数：pagedir_set_dirty
   功能：在PD中为虚拟页VPAGE的PTE设置脏位为DIRTY
   参数：pd - 页目录指针
         vpage - 虚拟页地址
         dirty - 是否设置为脏
   返回：无 */
void pagedir_set_dirty(uint32_t *pd, const void *vpage, bool dirty)
{
    uint32_t *pte = lookup_page(pd, vpage, false);
    if (pte != NULL) {
        if (dirty)
            *pte |= PTE_D;
        else {
            *pte &= ~(uint32_t) PTE_D;
            invalidate_pagedir(pd);
        }
    }
}

/* 函数：pagedir_is_accessed
   功能：检查PD中虚拟页VPAGE的PTE最近是否被访问过
   描述：即在安装PTE的时间和上次清除它之间的时间
         如果PD不包含VPAGE的PTE，则返回false
   参数：pd - 页目录指针
         vpage - 虚拟页地址
   返回：被访问过返回true，否则返回false */
bool pagedir_is_accessed(uint32_t *pd, const void *vpage)
{
    uint32_t *pte = lookup_page(pd, vpage, false);
    return pte != NULL && (*pte & PTE_A) != 0;
}

/* 函数：pagedir_set_accessed
   功能：在PD中为虚拟页VPAGE的PTE设置访问位为ACCESSED
   参数：pd - 页目录指针
         vpage - 虚拟页地址
         accessed - 是否设置为已访问
   返回：无 */
void pagedir_set_accessed(uint32_t *pd, const void *vpage, bool accessed)
{
    uint32_t *pte = lookup_page(pd, vpage, false);
    if (pte != NULL) {
        if (accessed)
            *pte |= PTE_A;
        else {
            *pte &= ~(uint32_t) PTE_A;
            invalidate_pagedir(pd);
        }
    }
}

/* 函数：pagedir_activate
   功能：将页目录PD加载到CPU的页目录基址寄存器中
   参数：pd - 要激活的页目录指针
   返回：无 */
void pagedir_activate(uint32_t *pd)
{
    if (pd == NULL)
        pd = init_page_dir;

    /* 将页目录的物理地址存储到CR3中，即PDBR（页目录基址寄存器）
       这会立即激活我们的新页表
       参见[IA32-v2a] "MOV--Move to/from Control Registers"和
       [IA32-v3a] 3.7.5 "Base Address of the Page Directory" */
    asm volatile("movl %0, %%cr3" : : "r"(vtop(pd)) : "memory");
}

/* 函数：active_pd
   功能：返回当前活动的页目录
   参数：无
   返回：当前活动页目录的指针 */
static uint32_t *
active_pd(void)
{
    /* 将CR3（页目录基址寄存器，PDBR）复制到`pd'中
       参见[IA32-v2a] "MOV--Move to/from Control Registers"和
       [IA32-v3a] 3.7.5 "Base Address of the Page Directory" */
    uintptr_t pd;
    asm volatile("movl %%cr3, %0" : "=r"(pd));
    return ptov(pd);
}

/* 函数：invalidate_pagedir
   功能：某些页表更改可能导致CPU的转换旁路缓冲区(TLB)与页表不同步
   描述：当这种情况发生时，我们必须通过重新激活它来"使"TLB"无效"
         如果PD是活动页目录，此函数会使TLB无效
         （如果PD不是活动的，则它的条目不在TLB中，所以不需要使任何东西无效）
   参数：pd - 页目录指针
   返回：无 */
static void
invalidate_pagedir(uint32_t *pd)
{
    if (active_pd() == pd) {
        /* 重新激活PD会清除TLB
           参见[IA32-v3a] 3.12 "Translation Lookaside Buffers (TLBs)" */
        pagedir_activate(pd);
    }
}
