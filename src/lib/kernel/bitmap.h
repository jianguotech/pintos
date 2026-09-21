#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/* 位图抽象数据类型。 */

/* 创建和销毁。 */
struct bitmap *bitmap_create(size_t bit_cnt);
struct bitmap *bitmap_create_in_buf(size_t bit_cnt, void *, size_t byte_cnt);
size_t bitmap_buf_size(size_t bit_cnt);
void bitmap_destroy(struct bitmap *);

/* 位图大小。 */
size_t bitmap_size(const struct bitmap *);

/* 设置和测试单个位。 */
void bitmap_set(struct bitmap *, size_t idx, bool);
void bitmap_mark(struct bitmap *, size_t idx);
void bitmap_reset(struct bitmap *, size_t idx);
void bitmap_flip(struct bitmap *, size_t idx);
bool bitmap_test(const struct bitmap *, size_t idx);

/* 设置和测试多个位。 */
void bitmap_set_all(struct bitmap *, bool);
void bitmap_set_multiple(struct bitmap *, size_t start, size_t cnt, bool);
size_t bitmap_count(const struct bitmap *, size_t start, size_t cnt, bool);
bool bitmap_contains(const struct bitmap *, size_t start, size_t cnt, bool);
bool bitmap_any(const struct bitmap *, size_t start, size_t cnt);
bool bitmap_none(const struct bitmap *, size_t start, size_t cnt);
bool bitmap_all(const struct bitmap *, size_t start, size_t cnt);

/* 查找设置或未设置的位。 */
#define BITMAP_ERROR SIZE_MAX
size_t bitmap_scan(const struct bitmap *, size_t start, size_t cnt, bool);
size_t bitmap_scan_and_flip(struct bitmap *, size_t start, size_t cnt, bool);

/* 文件输入输出。 */
#ifdef FILESYS
struct file;
size_t bitmap_file_size(const struct bitmap *);
bool bitmap_read(struct bitmap *, struct file *);
bool bitmap_write(const struct bitmap *, struct file *);
#endif

/* 调试。 */
void bitmap_dump(const struct bitmap *);

#endif /* lib/kernel/bitmap.h */
