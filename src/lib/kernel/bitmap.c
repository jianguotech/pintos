#include "bitmap.h"

#include "threads/malloc.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* 元素类型。

   必须是至少与int一样宽的无符号整数类型。

   每个位代表位图中的一个位。
   如果元素中的第0位代表位图中的第K位，
   那么元素中的第1位代表位图中的第K+1位，
   依此类推。 */
typedef unsigned long elem_type;

/* 元素中的位数。 */
#define ELEM_BITS (sizeof(elem_type) * CHAR_BIT)

/* 从外部看，位图是一个位数组。从内部看，
   它是一个elem_type数组（如上所定义），模拟一个位数组。 */
struct bitmap {
    size_t bit_cnt;  /* 位数。 */
    elem_type *bits; /* 表示位的元素。 */
};

/* 返回包含位号为BIT_IDX的位的元素的索引。 */
static inline size_t
elem_idx(size_t bit_idx)
{
    return bit_idx / ELEM_BITS;
}

/* 返回一个elem_type，其中只有对应于BIT_IDX的位被打开。 */
static inline elem_type
bit_mask(size_t bit_idx)
{
    return (elem_type) 1 << (bit_idx % ELEM_BITS);
}

/* 返回BIT_CNT位所需的元素数。 */
static inline size_t
elem_cnt(size_t bit_cnt)
{
    return DIV_ROUND_UP(bit_cnt, ELEM_BITS);
}

/* 返回BIT_CNT位所需的字节数。 */
static inline size_t
byte_cnt(size_t bit_cnt)
{
    return sizeof(elem_type) * elem_cnt(bit_cnt);
}

/* 返回一个位掩码，其中B的位数组最后一个元素中
   实际使用的位被设置为1，其余为0。 */
static inline elem_type
last_mask(const struct bitmap *b)
{
    int last_bits = b->bit_cnt % ELEM_BITS;
    return last_bits ? ((elem_type) 1 << last_bits) - 1 : (elem_type) -1;
}

/* 创建和销毁。 */

/* 创建并返回一个指向新分配的位图的指针，该位图有BIT_CNT个（或更多）位的空间。
   如果内存分配失败，返回空指针。
   调用者负责在不再需要时使用bitmap_destroy()释放该位图。 */
struct bitmap *
bitmap_create(size_t bit_cnt)
{
    struct bitmap *b = malloc(sizeof *b);
    if (b != NULL) {
        b->bit_cnt = bit_cnt;
        b->bits = malloc(byte_cnt(bit_cnt));
        if (b->bits != NULL || bit_cnt == 0) {
            bitmap_set_all(b, false);
            return b;
        }
        free(b);
    }
    return NULL;
}

/* 创建并返回一个位图，有BIT_CNT位，
   在预分配的BLOCK中的BLOCK_SIZE字节存储中。
   BLOCK_SIZE必须至少为bitmap_needed_bytes(BIT_CNT)。 */
struct bitmap *
bitmap_create_in_buf(size_t bit_cnt, void *block, size_t block_size UNUSED)
{
    struct bitmap *b = block;

    ASSERT(block_size >= bitmap_buf_size(bit_cnt));

    b->bit_cnt = bit_cnt;
    b->bits = (elem_type *) (b + 1);
    bitmap_set_all(b, false);
    return b;
}

/* 返回容纳有BIT_CNT位的位图所需的字节数
   （用于bitmap_create_in_buf()）。 */
size_t
bitmap_buf_size(size_t bit_cnt)
{
    return sizeof(struct bitmap) + byte_cnt(bit_cnt);
}

/* 销毁位图B，释放其存储空间。
   不用于bitmap_create_in_buf()创建的位图。 */
void bitmap_destroy(struct bitmap *b)
{
    if (b != NULL) {
        free(b->bits);
        free(b);
    }
}

/* 位图大小。 */

/* 返回B中的位数。 */
size_t
bitmap_size(const struct bitmap *b)
{
    return b->bit_cnt;
}

/* 设置和测试单个位。 */

/* 原子地将B中索引为IDX的位设置为VALUE。 */
void bitmap_set(struct bitmap *b, size_t idx, bool value)
{
    ASSERT(b != NULL);
    ASSERT(idx < b->bit_cnt);
    if (value)
        bitmap_mark(b, idx);
    else
        bitmap_reset(b, idx);
}

/* 原子地将B中位号为BIT_IDX的位设置为true。 */
void bitmap_mark(struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx(bit_idx);
    elem_type mask = bit_mask(bit_idx);

    /* 这等同于`b->bits[idx] |= mask'，除了它
       保证在单处理器机器上是原子的。参见
       [IA32-v2b]中OR指令的描述。 */
    asm("orl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* 原子地将B中位号为BIT_IDX的位设置为false。 */
void bitmap_reset(struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx(bit_idx);
    elem_type mask = bit_mask(bit_idx);

    /* 这等同于`b->bits[idx] &= ~mask'，除了它
       保证在单处理器机器上是原子的。参见
       [IA32-v2a]中AND指令的描述。 */
    asm("andl %1, %0" : "=m"(b->bits[idx]) : "r"(~mask) : "cc");
}

/* 原子地切换B中索引为IDX的位；
   也就是说，如果它为true，将其设置为false，
   如果它为false，将其设置为true。 */
void bitmap_flip(struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx(bit_idx);
    elem_type mask = bit_mask(bit_idx);

    /* 这等同于`b->bits[idx] ^= mask'，除了它
       保证在单处理器机器上是原子的。参见
       [IA32-v2b]中XOR指令的描述。 */
    asm("xorl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* 返回B中索引为IDX的位的值。 */
bool bitmap_test(const struct bitmap *b, size_t idx)
{
    ASSERT(b != NULL);
    ASSERT(idx < b->bit_cnt);
    return (b->bits[elem_idx(idx)] & bit_mask(idx)) != 0;
}

/* 设置和测试多个位。 */

/* 将B中的所有位设置为VALUE。 */
void bitmap_set_all(struct bitmap *b, bool value)
{
    ASSERT(b != NULL);

    bitmap_set_multiple(b, 0, bitmap_size(b), value);
}

/* 将B中从START开始的CNT个位设置为VALUE。 */
void bitmap_set_multiple(struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i;

    ASSERT(b != NULL);
    ASSERT(start <= b->bit_cnt);
    ASSERT(start + cnt <= b->bit_cnt);

    for (i = 0; i < cnt; i++)
        bitmap_set(b, start + i, value);
}

/* 返回B中从START到START + CNT之间（不包括START + CNT）
   设置为VALUE的位数。 */
size_t
bitmap_count(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i, value_cnt;

    ASSERT(b != NULL);
    ASSERT(start <= b->bit_cnt);
    ASSERT(start + cnt <= b->bit_cnt);

    value_cnt = 0;
    for (i = 0; i < cnt; i++)
        if (bitmap_test(b, start + i) == value)
            value_cnt++;
    return value_cnt;
}

/* 返回真值，如果B中从START到START + CNT之间（不包括START + CNT）
   的任何位设置为VALUE，否则返回假值。 */
bool bitmap_contains(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i;

    ASSERT(b != NULL);
    ASSERT(start <= b->bit_cnt);
    ASSERT(start + cnt <= b->bit_cnt);

    for (i = 0; i < cnt; i++)
        if (bitmap_test(b, start + i) == value)
            return true;
    return false;
}

/* 返回真值，如果B中从START到START + CNT之间（不包括START + CNT）
   的任何位设置为true，否则返回假值。*/
bool bitmap_any(const struct bitmap *b, size_t start, size_t cnt)
{
    return bitmap_contains(b, start, cnt, true);
}

/* 返回真值，如果B中从START到START + CNT之间（不包括START + CNT）
   没有任何位设置为true，否则返回假值。*/
bool bitmap_none(const struct bitmap *b, size_t start, size_t cnt)
{
    return !bitmap_contains(b, start, cnt, true);
}

/* 返回真值，如果B中从START到START + CNT之间（不包括START + CNT）
   的每一位都设置为true，否则返回假值。 */
bool bitmap_all(const struct bitmap *b, size_t start, size_t cnt)
{
    return !bitmap_contains(b, start, cnt, false);
}

/* 查找设置或未设置的位。 */

/* 查找并返回B中从START开始或之后的第一组CNT个
   连续位的起始索引，这些位都设置为VALUE。
   如果没有这样的组，返回BITMAP_ERROR。 */
size_t
bitmap_scan(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    ASSERT(b != NULL);
    ASSERT(start <= b->bit_cnt);

    if (cnt <= b->bit_cnt) {
        size_t last = b->bit_cnt - cnt;
        size_t i;
        for (i = start; i <= last; i++)
            if (!bitmap_contains(b, i, cnt, !value))
                return i;
    }
    return BITMAP_ERROR;
}

/* 查找B中从START开始或之后的第一组CNT个连续位，
   这些位都设置为VALUE，将它们全部翻转为!VALUE，
   并返回该组中第一位的索引。
   如果没有这样的组，返回BITMAP_ERROR。
   如果CNT为零，返回0。
   位被原子地设置，但测试位对设置位不是原子的。 */
size_t
bitmap_scan_and_flip(struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t idx = bitmap_scan(b, start, cnt, value);
    if (idx != BITMAP_ERROR)
        bitmap_set_multiple(b, idx, cnt, !value);
    return idx;
}

/* 文件输入和输出。 */

#ifdef FILESYS
/* 返回在文件中存储B所需的字节数。 */
size_t
bitmap_file_size(const struct bitmap *b)
{
    return byte_cnt(b->bit_cnt);
}

/* 从FILE读取B。如果成功返回真，否则返回假。 */
bool bitmap_read(struct bitmap *b, struct file *file)
{
    bool success = true;
    if (b->bit_cnt > 0) {
        off_t size = byte_cnt(b->bit_cnt);
        success = file_read_at(file, b->bits, size, 0) == size;
        b->bits[elem_cnt(b->bit_cnt) - 1] &= last_mask(b);
    }
    return success;
}

/* 将B写入FILE。如果成功返回真，否则返回假。 */
bool bitmap_write(const struct bitmap *b, struct file *file)
{
    off_t size = byte_cnt(b->bit_cnt);
    return file_write_at(file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* 调试。 */

/* 将B的内容转储到控制台作为十六进制。 */
void bitmap_dump(const struct bitmap *b)
{
    hex_dump(0, b->bits, byte_cnt(b->bit_cnt), false);
}
