#include "threads/malloc.h"

#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 简易的 malloc() 实现。

   每次分配的字节大小会向上取整到不小于请求大小的最小 2 的幂，
   然后映射到对应的描述符（descriptor）。每个描述符维护一个空闲块
   列表；当列表非空时直接从其中取出块满足分配请求。

   若对应描述符的空闲列表为空，则从页分配器获取一页作为新的
   "arena"，将该 arena 划分为多个块并加入描述符的空闲列表，随后
   返回其中一个块。

   释放时将块放回描述符的空闲列表；如果某个 arena 的所有块都
   未被使用，则将该 arena 的所有块从空闲列表移除并把该页归还给
   页分配器。

   该方案无法处理超过 2 KB 的大块（无法在单页内附带描述符），
   针对大块会直接按需分配连续页，并在 arena 头部记录页数。 */

/* 描述符结构体。
   每个描述符管理一种固定大小的内存块，维护该类块的空闲列表和互斥锁。 */
struct desc {
    size_t block_size;       /* 每个块的字节大小。 */
    size_t blocks_per_arena; /* 每个 arena 包含的块数量。 */
    struct list free_list;   /* 空闲块链表。 */
    struct lock lock;        /* 保护该描述符的互斥锁。 */
};

/* 用于检测 arena 结构损坏的魔数。 */
#define ARENA_MAGIC 0x9a548eed

/* Arena（竞技场/内存池）结构体。
   每个 arena 占用一个内存页，头部是管理信息，其余部分被划分为
   相同大小的块用于内存分配。 */
struct arena {
    unsigned magic;    /* 始终设置为 ARENA_MAGIC 用于校验。 */
    struct desc *desc; /* 所属描述符，大块内存时为 null。 */
    size_t free_cnt;   /* 空闲块数量；对于大块内存表示页数。 */
};

/* 空闲块结构体。
   当块未被分配时，通过此结构体将块链入描述符的空闲列表。
   被分配后，整个块都供用户使用，此结构体不存在。 */
struct block {
    struct list_elem free_elem; /* 链接空闲块链表的元素。 */
};

/* 全局描述符表和相关变量。 */
static struct desc descs[10]; /* 描述符数组，支持不同大小的内存块。 */
static size_t desc_cnt;       /* 实际使用的描述符数量。 */

static struct arena *block_to_arena(struct block *);
static struct block *arena_to_block(struct arena *, size_t idx);

/* 函数：malloc_init
   功能：初始化内存分配器的尺寸描述符数组，为不同块大小建表。
   参数：无。
   返回：无。 */
void malloc_init(void)
{
    size_t block_size;

    for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
        struct desc *d = &descs[desc_cnt++];
        ASSERT(desc_cnt <= sizeof descs / sizeof *descs);
        d->block_size = block_size;
        d->blocks_per_arena = (PGSIZE - sizeof(struct arena)) / block_size;
        list_init(&d->free_list);
        lock_init(&d->lock);
    }
}

/* 函数：malloc
   功能：申请至少 size 字节的内存块；若失败返回 NULL。
   参数：size —— 需求的最小字节数。
   返回：成功返回可用内存首地址，失败返回 NULL。 */
void *
malloc(size_t size)
{
    struct desc *d;
    struct block *b;
    struct arena *a;

    /* 如果请求 0 字节，返回 NULL 指针以满足语义。 */
    if (size == 0)
        return NULL;

    /* 查找满足 SIZE 字节请求的最小描述符。 */
    for (d = descs; d < descs + desc_cnt; d++)
        if (d->block_size >= size)
            break;
    if (d == descs + desc_cnt) {
        /* SIZE 对所有描述符都太大。
           直接分配足够的页来容纳 SIZE 加上 arena 头部。 */
        size_t page_cnt = DIV_ROUND_UP(size + sizeof *a, PGSIZE);
        a = palloc_get_multiple(0, page_cnt);
        if (a == NULL)
            return NULL;

        /* 初始化 arena 以表示包含 PAGE_CNT 页的大块，并返回块地址。 */
        a->magic = ARENA_MAGIC;
        a->desc = NULL;
        a->free_cnt = page_cnt;
        return a + 1;
    }

    lock_acquire(&d->lock);

    /* 如果空闲列表为空，创建一个新的 arena。 */
    if (list_empty(&d->free_list)) {
        size_t i;

        /* 分配一页内存。 */
        a = palloc_get_page(0);
        if (a == NULL) {
            lock_release(&d->lock);
            return NULL;
        }

        /* 初始化 arena 并将其块加入空闲列表。 */
        a->magic = ARENA_MAGIC;
        a->desc = d;
        a->free_cnt = d->blocks_per_arena;
        for (i = 0; i < d->blocks_per_arena; i++) {
            struct block *b = arena_to_block(a, i);
            list_push_back(&d->free_list, &b->free_elem);
        }
    }

    /* 从空闲列表获取一个块并返回。 */
    b = list_entry(list_pop_front(&d->free_list), struct block, free_elem);
    a = block_to_arena(b);
    a->free_cnt--;
    lock_release(&d->lock);
    return b;
}

/* 函数：calloc
   功能：申请 a*b 字节的内存并置零；溢出或分配失败返回 NULL。
   参数：a —— 元素个数；b —— 元素大小。
   返回：成功返回零初始化的内存块，失败返回 NULL。 */
void *
calloc(size_t a, size_t b)
{
    void *p;
    size_t size;

    /* 计算块大小并确保其能容纳于 size_t 类型中。 */
    size = a * b;
    if (size < a || size < b)
        return NULL;

    /* 分配内存并清零。 */
    p = malloc(size);
    if (p != NULL)
        memset(p, 0, size);

    return p;
}

/* Returns the number of bytes allocated for BLOCK. */
static size_t
block_size(void *block)
{
    struct block *b = block;
    struct arena *a = block_to_arena(b);
    struct desc *d = a->desc;

    return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs(block);
}

/* 函数：realloc
   功能：重新调整 old_block 的大小为 new_size，可能移动到新位置。
   参数：old_block —— 原指针，可为空；new_size —— 新容量，0 表示释放。
   返回：成功返回有效指针，失败返回 NULL。 */
void *
realloc(void *old_block, size_t new_size)
{
    if (new_size == 0) {
        free(old_block);
        return NULL;
    }
    else {
        void *new_block = malloc(new_size);
        if (old_block != NULL && new_block != NULL) {
            size_t old_size = block_size(old_block);
            size_t min_size = new_size < old_size ? new_size : old_size;
            memcpy(new_block, old_block, min_size);
            free(old_block);
        }
        return new_block;
    }
}

/* 函数：free
   功能：释放之前由 malloc/calloc/realloc 获取的内存块。
   参数：p —— 待释放指针，可为空。
   返回：无。 */
void free(void *p)
{
    if (p != NULL) {
        struct block *b = p;
        struct arena *a = block_to_arena(b);
        struct desc *d = a->desc;

        if (d != NULL) {
            /* It's a normal block.  We handle it here. */

#ifndef NDEBUG
            /* Clear the block to help detect use-after-free bugs. */
            memset(b, 0xcc, d->block_size);
#endif

            lock_acquire(&d->lock);

            /* Add block to free list. */
            list_push_front(&d->free_list, &b->free_elem);

            /* If the arena is now entirely unused, free it. */
            if (++a->free_cnt >= d->blocks_per_arena) {
                size_t i;

                ASSERT(a->free_cnt == d->blocks_per_arena);
                for (i = 0; i < d->blocks_per_arena; i++) {
                    struct block *b = arena_to_block(a, i);
                    list_remove(&b->free_elem);
                }
                palloc_free_page(a);
            }

            lock_release(&d->lock);
        }
        else {
            /* It's a big block.  Free its pages. */
            palloc_free_multiple(a, a->free_cnt);
            return;
        }
    }
}

/* 函数：block_to_arena
   功能：从块地址回溯求出所在的 arena 元信息，并执行合法性校验。
   参数：b —— 内存块指针。
   返回：对应 arena 指针。 */
static struct arena *
block_to_arena(struct block *b)
{
    struct arena *a = pg_round_down(b);

    /* Check that the arena is valid. */
    ASSERT(a != NULL);
    ASSERT(a->magic == ARENA_MAGIC);

    /* Check that the block is properly aligned for the arena. */
    ASSERT(a->desc == NULL || (pg_ofs(b) - sizeof *a) % a->desc->block_size == 0);
    ASSERT(a->desc != NULL || pg_ofs(b) == sizeof *a);

    return a;
}

/* 函数：arena_to_block
   功能：根据 arena 和索引定位到第 idx 个块（0 基）。
   参数：a —— arena 指针；idx —— 块序号。
   返回：对应的 block 指针。 */
static struct block *
arena_to_block(struct arena *a, size_t idx)
{
    ASSERT(a != NULL);
    ASSERT(a->magic == ARENA_MAGIC);
    ASSERT(idx < a->desc->blocks_per_arena);
    return (struct block *) ((uint8_t *) a + sizeof *a + idx * a->desc->block_size);
}
