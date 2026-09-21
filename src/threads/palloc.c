#include "threads/palloc.h"

#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 页分配器（page allocator）。按页（或页的倍数）分配内存。

   系统内存被划分为两个池：kernel pool 与 user pool。user pool
   用于用户虚拟内存页，kernel pool 用于内核自身及其它用途，
   该策略确保即使用户进程大量换出，内核也能获得必要的内存。

   默认情况下可用内存的一半分配给 kernel pool，另一半给 user pool，
   这对教学示例来说已经足够。 */

/* 内存池结构体。 */
struct pool {
    struct lock lock;        /* 互斥锁，保护对池的并发访问。 */
    struct bitmap *used_map; /* 已用页的位图。 */
    uint8_t *base;           /* 池的基地址。 */
};

/* 两个内存池：一个给内核数据使用，一个给用户页面使用。 */
static struct pool kernel_pool, user_pool;

static void init_pool(struct pool *, void *base, size_t page_cnt, const char *name);
static bool page_from_pool(const struct pool *, void *page);

/* 函数：palloc_init
   功能：初始化页分配器，按照限制将物理页划分到内核池与用户池。
   参数：user_page_limit —— 用户池允许分配的最大页数。
   返回：无。 */
/* 初始化页分配器。最多将 USER_PAGE_LIMIT
   页放入用户池。 */
void palloc_init(size_t user_page_limit)
{
    /* 可用内存从 1 MB 开始，一直到 RAM 末尾。 */
    uint8_t *free_start = ptov(1024 * 1024);
    uint8_t *free_end = ptov(init_ram_pages * PGSIZE);
    size_t free_pages = (free_end - free_start) / PGSIZE;
    size_t user_pages = free_pages / 2;
    size_t kernel_pages;
    if (user_pages > user_page_limit)
        user_pages = user_page_limit;
    kernel_pages = free_pages - user_pages;

    /* 将一半内存分配给内核，一半分配给用户。 */
    init_pool(&kernel_pool, free_start, kernel_pages, "kernel pool");
    init_pool(&user_pool, free_start + kernel_pages * PGSIZE, user_pages, "user pool");
}

/* 函数：palloc_get_multiple
   功能：获取 PAGE_CNT 个连续空闲页。可通过 FLAGS 指定来源池（PAL_USER）
         和是否清零（PAL_ZERO），或在分配失败时触发内核恐慌（PAL_ASSERT）。
   返回：成功返回页的内核虚拟地址，失败返回 NULL（或在 PAL_ASSERT 时 panic）。 */
void *
palloc_get_multiple(enum palloc_flags flags, size_t page_cnt)
{
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *pages;
    size_t page_idx;

    if (page_cnt == 0)
        return NULL;

    lock_acquire(&pool->lock);
    page_idx = bitmap_scan_and_flip(pool->used_map, 0, page_cnt, false);
    lock_release(&pool->lock);

    if (page_idx != BITMAP_ERROR)
        pages = pool->base + PGSIZE * page_idx;
    else
        pages = NULL;

    if (pages != NULL) {
        if (flags & PAL_ZERO)
            memset(pages, 0, PGSIZE * page_cnt);
    }
    else {
        if (flags & PAL_ASSERT)
            PANIC("palloc_get: out of pages");
    }

    return pages;
}

/* 函数：palloc_get_page
   功能：获取单页内存（palloc_get_multiple 的包装）。
   参数：flags（同 palloc_get_multiple）。
   返回：成功返回页的内核虚拟地址，失败返回 NULL（或在 PAL_ASSERT 时 panic）。 */
void *
palloc_get_page(enum palloc_flags flags)
{
    return palloc_get_multiple(flags, 1);
}

/* 函数：palloc_free_multiple
   功能：释放从 pages 起连续的 page_cnt 个页，自动识别所属池。
   参数：pages —— 待释放的页起始；page_cnt —— 连续页数。
   返回：无。 */
void palloc_free_multiple(void *pages, size_t page_cnt)
{
    struct pool *pool;
    size_t page_idx;

    ASSERT(pg_ofs(pages) == 0);
    if (pages == NULL || page_cnt == 0)
        return;

    if (page_from_pool(&kernel_pool, pages))
        pool = &kernel_pool;
    else if (page_from_pool(&user_pool, pages))
        pool = &user_pool;
    else
        NOT_REACHED();

    page_idx = pg_no(pages) - pg_no(pool->base);

#ifndef NDEBUG
    memset(pages, 0xcc, PGSIZE * page_cnt);
#endif

    ASSERT(bitmap_all(pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple(pool->used_map, page_idx, page_cnt, false);
}

/* 函数：palloc_free_page
   功能：释放单个页，是 palloc_free_multiple 的封装。
   参数：page —— 待释放页指针。
   返回：无。 */
void palloc_free_page(void *page)
{
    palloc_free_multiple(page, 1);
}

/* 函数：init_pool
   功能：初始化物理页池，计算并为位图保留空间，记录基址与容量。
   参数：p —— 目标池；base —— 物理起始地址；page_cnt —— 页总数；
         name —— 用于调试输出的名称。 */
static void
init_pool(struct pool *p, void *base, size_t page_cnt, const char *name)
{
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    size_t bm_pages = DIV_ROUND_UP(bitmap_buf_size(page_cnt), PGSIZE);
    if (bm_pages > page_cnt)
        PANIC("Not enough memory in %s for bitmap.", name);
    page_cnt -= bm_pages;

    printf("%zu pages available in %s.\n", page_cnt, name);

    /* Initialize the pool. */
    lock_init(&p->lock);
    p->used_map = bitmap_create_in_buf(page_cnt, base, bm_pages * PGSIZE);
    p->base = base + bm_pages * PGSIZE;
}

/* 函数：page_from_pool
   功能：判断给定页地址是否属于指定的页池。
   参数：pool —— 目标页池；page —— 待检查的页地址。
   返回：若页属于该池则返回 true，否则返回 false。 */
static bool
page_from_pool(const struct pool *pool, void *page)
{
    size_t page_no = pg_no(page);
    size_t start_page = pg_no(pool->base);
    size_t end_page = start_page + bitmap_size(pool->used_map);

    return page_no >= start_page && page_no < end_page;
}
