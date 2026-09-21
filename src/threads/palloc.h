#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>

/* 页分配标志。 */
enum palloc_flags {
    PAL_ASSERT = 001, /* 失败时触发 panic。 */
    PAL_ZERO = 002,   /* 分配后清零页面内容。 */
    PAL_USER = 004    /* 从用户池分配页面。 */
};

/* 函数：palloc_init
   功能：初始化页分配器并设定用户池限制
   参数：user_page_limit - 用户池的页数限制
   返回：无 */
void palloc_init(size_t user_page_limit);
/* 函数：palloc_get_page
   功能：按 flags 从合适的页池获取单个页
   参数：flags - 页分配标志（PAL_ASSERT/PAL_ZERO/PAL_USER）
   返回：分配页的内核虚拟地址，失败返回 NULL */
void *palloc_get_page(enum palloc_flags);
/* 函数：palloc_get_multiple
   功能：分配 page_cnt 个连续页
   参数：flags - 页分配标志，page_cnt - 要分配的页数
   返回：分配页块的起始内核虚拟地址，失败返回 NULL */
void *palloc_get_multiple(enum palloc_flags, size_t page_cnt);
/* 函数：palloc_free_page
   功能：释放单个页
   参数：page - 要释放的页地址
   返回：无 */
void palloc_free_page(void *);
/* 函数：palloc_free_multiple
   功能：释放连续页块
   参数：pages - 要释放的页块起始地址，page_cnt - 页块中的页数
   返回：无 */
void palloc_free_multiple(void *, size_t page_cnt);

#endif /* threads/palloc.h */
