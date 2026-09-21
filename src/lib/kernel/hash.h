#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* 哈希表。

   此数据结构在 Pintos 项目 3 的导览中有详细记录。

   这是一个标准的链式哈希表。要定位表中的元素，我们计算元素
   数据上的哈希函数，并将其用作双链表数组的索引，然后线性搜索列表。

   链表不使用动态分配。相反，每个可能在哈希中的结构都必须嵌入
   一个 struct hash_elem 成员。所有哈希函数都在这些 `struct hash_elem'
   上操作。hash_entry 宏允许从 struct hash_elem 转换回包含它的结构体。
   这与链表实现中使用的技术相同。有关详细说明，请参阅 lib/kernel/list.h。 */

#include "list.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 哈希元素。 */
struct hash_elem {
    struct list_elem list_elem;
};

/* 将指向哈希元素 HASH_ELEM 的指针转换为指向包含 HASH_ELEM 的结构体
   的指针。提供外部结构体 STRUCT 的名称和哈希元素 MEMBER 的成员名称。
   参见文件顶部的详细注释以获取示例。 */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER) \
    ((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem - offsetof(STRUCT, MEMBER.list_elem)))

/* 计算并返回哈希元素 E 的哈希值，给定辅助数据 AUX。 */
typedef unsigned hash_hash_func(const struct hash_elem *e, void *aux);

/* 比较两个哈希元素 A 和 B 的值，给定辅助数据 AUX。如果 A 小于 B
   则返回真，否则返回假（即 A 大于或等于 B）。 */
typedef bool hash_less_func(const struct hash_elem *a,
                            const struct hash_elem *b,
                            void *aux);

/* 对哈希元素 E 执行某些操作，给定辅助数据 AUX。 */
typedef void hash_action_func(struct hash_elem *e, void *aux);

/* 哈希表。 */
struct hash {
    size_t elem_cnt;      /* 表中的元素数。 */
    size_t bucket_cnt;    /* 桶的数量，是 2 的幂。 */
    struct list *buckets; /* `bucket_cnt' 个列表的数组。 */
    hash_hash_func *hash; /* 哈希函数。 */
    hash_less_func *less; /* 比较函数。 */
    void *aux;            /* `hash' 和 `less' 的辅助数据。 */
};

/* 哈希表迭代器。 */
struct hash_iterator {
    struct hash *hash;      /* 哈希表。 */
    struct list *bucket;    /* 当前桶。 */
    struct hash_elem *elem; /* 当前桶中的当前哈希元素。 */
};

/* 基本生命周期。 */
bool hash_init(struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear(struct hash *, hash_action_func *);
void hash_destroy(struct hash *, hash_action_func *);

/* 搜索、插入、删除。 */
struct hash_elem *hash_insert(struct hash *, struct hash_elem *);
struct hash_elem *hash_replace(struct hash *, struct hash_elem *);
struct hash_elem *hash_find(struct hash *, struct hash_elem *);
struct hash_elem *hash_delete(struct hash *, struct hash_elem *);

/* 迭代。 */
void hash_apply(struct hash *, hash_action_func *);
void hash_first(struct hash_iterator *, struct hash *);
struct hash_elem *hash_next(struct hash_iterator *);
struct hash_elem *hash_cur(struct hash_iterator *);

/* 信息。 */
size_t hash_size(struct hash *);
bool hash_empty(struct hash *);

/* 示例哈希函数。 */
unsigned hash_bytes(const void *, size_t);
unsigned hash_string(const char *);
unsigned hash_int(int);

#endif /* lib/kernel/hash.h */
