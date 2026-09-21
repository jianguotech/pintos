/* 哈希表。

   该数据结构在 Pintos 第 3 个项目的教程中有详细说明。

   有关基本信息，请参阅 hash.h。 */

#include "hash.h"

#include "../debug.h"
#include "threads/malloc.h"

#define list_elem_to_hash_elem(LIST_ELEM) \
    list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket(struct hash *, struct hash_elem *);
static struct hash_elem *find_elem(struct hash *, struct list *, struct hash_elem *);
static void insert_elem(struct hash *, struct list *, struct hash_elem *);
static void remove_elem(struct hash *, struct hash_elem *);
static void rehash(struct hash *);

/* 使用 HASH 计算哈希值，使用 LESS 比较哈希元素来初始化哈希表 H，给定辅助数据 AUX。 */
bool hash_init(struct hash *h,
               hash_hash_func *hash,
               hash_less_func *less,
               void *aux)
{
    h->elem_cnt = 0;
    h->bucket_cnt = 4;
    h->buckets = malloc(sizeof *h->buckets * h->bucket_cnt);
    h->hash = hash;
    h->less = less;
    h->aux = aux;

    if (h->buckets != NULL) {
        hash_clear(h, NULL);
        return true;
    }
    else
        return false;
}

/* 从 H 中删除所有元素。

   如果 DESTRUCTOR 非空，则为哈希中的每个元素调用它。DESTRUCTOR 可以
   根据需要释放哈希元素使用的内存。但是，在 hash_clear() 运行时修改
   哈希表 H，使用任何 hash_clear()、hash_destroy()、hash_insert()、
   hash_replace() 或 hash_delete() 函数，无论是在 DESTRUCTOR 中还是
   在其他地方进行，都会产生未定义的行为。 */
void hash_clear(struct hash *h, hash_action_func *destructor)
{
    size_t i;

    for (i = 0; i < h->bucket_cnt; i++) {
        struct list *bucket = &h->buckets[i];

        if (destructor != NULL)
            while (!list_empty(bucket)) {
                struct list_elem *list_elem = list_pop_front(bucket);
                struct hash_elem *hash_elem = list_elem_to_hash_elem(list_elem);
                destructor(hash_elem, h->aux);
            }

        list_init(bucket);
    }

    h->elem_cnt = 0;
}

/* 销毁哈希表 H。

   如果 DESTRUCTOR 非空，则首先为哈希中的每个元素调用它。DESTRUCTOR 可以
   根据需要释放哈希元素使用的内存。但是，在 hash_clear() 运行时修改
   哈希表 H，使用任何 hash_clear()、hash_destroy()、hash_insert()、
   hash_replace() 或 hash_delete() 函数，无论是在 DESTRUCTOR 中还是
   在其他地方进行，都会产生未定义的行为。 */
void hash_destroy(struct hash *h, hash_action_func *destructor)
{
    if (destructor != NULL)
        hash_clear(h, destructor);
    free(h->buckets);
}

/* 将 NEW 插入哈希表 H 并返回空指针（如果表中尚无相等元素）。
   如果表中已存在相等元素，则返回该元素，不插入 NEW。 */
struct hash_elem *
hash_insert(struct hash *h, struct hash_elem *new)
{
    struct list *bucket = find_bucket(h, new);
    struct hash_elem *old = find_elem(h, bucket, new);

    if (old == NULL)
        insert_elem(h, bucket, new);

    rehash(h);

    return old;
}

/* 将 NEW 插入哈希表 H，替换表中任何相等的元素，并返回该元素。 */
struct hash_elem *
hash_replace(struct hash *h, struct hash_elem *new)
{
    struct list *bucket = find_bucket(h, new);
    struct hash_elem *old = find_elem(h, bucket, new);

    if (old != NULL)
        remove_elem(h, old);
    insert_elem(h, bucket, new);

    rehash(h);

    return old;
}

/* 查找并返回哈希表 H 中与 E 相等的元素，如果表中不存在相等元素，则返回空指针。 */
struct hash_elem *
hash_find(struct hash *h, struct hash_elem *e)
{
    return find_elem(h, find_bucket(h, e), e);
}

/* 查找、删除并返回哈希表 H 中与 E 相等的元素。如果表中不存在相等元素，
   则返回空指针。

   如果哈希表的元素是动态分配的，或拥有动态分配的资源，则调用者
   负责释放它们。 */
struct hash_elem *
hash_delete(struct hash *h, struct hash_elem *e)
{
    struct hash_elem *found = find_elem(h, find_bucket(h, e), e);
    if (found != NULL) {
        remove_elem(h, found);
        rehash(h);
    }
    return found;
}

/* 为哈希表 H 中的每个元素调用 ACTION，顺序任意。
   在 hash_apply() 运行时修改哈希表 H，使用任何 hash_clear()、hash_destroy()、
   hash_insert()、hash_replace() 或 hash_delete() 函数，无论是从 ACTION
   中还是在其他地方进行，都会产生未定义的行为。 */
void hash_apply(struct hash *h, hash_action_func *action)
{
    size_t i;

    ASSERT(action != NULL);

    for (i = 0; i < h->bucket_cnt; i++) {
        struct list *bucket = &h->buckets[i];
        struct list_elem *elem, *next;

        for (elem = list_begin(bucket); elem != list_end(bucket); elem = next) {
            next = list_next(elem);
            action(list_elem_to_hash_elem(elem), h->aux);
        }
    }
}

/* 为迭代哈希表 H 初始化 I。

   迭代习语：

      struct hash_iterator i;

      hash_first (&i, h);
      while (hash_next (&i))
        {
          struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
          ...对 f 执行某些操作...
        }

   在迭代过程中修改哈希表 H，使用任何 hash_clear()、hash_destroy()、
   hash_insert()、hash_replace() 或 hash_delete() 函数，会使所有迭代器无效。 */
void hash_first(struct hash_iterator *i, struct hash *h)
{
    ASSERT(i != NULL);
    ASSERT(h != NULL);

    i->hash = h;
    i->bucket = i->hash->buckets;
    i->elem = list_elem_to_hash_elem(list_head(i->bucket));
}

/* 将 I 前进到哈希表中的下一个元素并返回它。如果没有剩余元素，则返回空指针。
   元素以任意顺序返回。

   在迭代过程中修改哈希表 H，使用任何 hash_clear()、hash_destroy()、
   hash_insert()、hash_replace() 或 hash_delete() 函数，会使所有迭代器无效。 */
struct hash_elem *
hash_next(struct hash_iterator *i)
{
    ASSERT(i != NULL);

    i->elem = list_elem_to_hash_elem(list_next(&i->elem->list_elem));
    while (i->elem == list_elem_to_hash_elem(list_end(i->bucket))) {
        if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
            i->elem = NULL;
            break;
        }
        i->elem = list_elem_to_hash_elem(list_begin(i->bucket));
    }

    return i->elem;
}

/* 返回哈希表迭代中的当前元素，或在表末尾返回空指针。在调用 hash_first()
   之后但在调用 hash_next() 之前的行为未定义。 */
struct hash_elem *
hash_cur(struct hash_iterator *i)
{
    return i->elem;
}

/* 返回 H 中的元素数量。 */
size_t
hash_size(struct hash *h)
{
    return h->elem_cnt;
}

/* 如果 H 不包含任何元素则返回真，否则返回假。 */
bool hash_empty(struct hash *h)
{
    return h->elem_cnt == 0;
}

/* Fowler-Noll-Vo 哈希常数，用于 32 位字大小。 */
#define FNV_32_PRIME 16777619u
#define FNV_32_BASIS 2166136261u

/* 返回 BUF 中 SIZE 个字节的哈希值。 */
unsigned
hash_bytes(const void *buf_, size_t size)
{
    /* Fowler-Noll-Vo 32 位哈希，用于字节。 */
    const unsigned char *buf = buf_;
    unsigned hash;

    ASSERT(buf != NULL);

    hash = FNV_32_BASIS;
    while (size-- > 0)
        hash = (hash * FNV_32_PRIME) ^ *buf++;

    return hash;
}

/* 返回字符串 S 的哈希值。 */
unsigned
hash_string(const char *s_)
{
    const unsigned char *s = (const unsigned char *) s_;
    unsigned hash;

    ASSERT(s != NULL);

    hash = FNV_32_BASIS;
    while (*s != '\0')
        hash = (hash * FNV_32_PRIME) ^ *s++;

    return hash;
}

/* 返回整数 I 的哈希值。 */
unsigned
hash_int(int i)
{
    return hash_bytes(&i, sizeof i);
}

/* 返回 H 中 E 所属的桶。 */
static struct list *
find_bucket(struct hash *h, struct hash_elem *e)
{
    size_t bucket_idx = h->hash(e, h->aux) & (h->bucket_cnt - 1);
    return &h->buckets[bucket_idx];
}

/* 在 H 的 BUCKET 中搜索与 E 相等的哈希元素。如果找到则返回它，否则返回空指针。 */
static struct hash_elem *
find_elem(struct hash *h, struct list *bucket, struct hash_elem *e)
{
    struct list_elem *i;

    for (i = list_begin(bucket); i != list_end(bucket); i = list_next(i)) {
        struct hash_elem *hi = list_elem_to_hash_elem(i);
        if (!h->less(hi, e, h->aux) && !h->less(e, hi, h->aux))
            return hi;
    }
    return NULL;
}

/* 返回 X，其最低有效位设置为 1 的位已关闭。 */
static inline size_t
turn_off_least_1bit(size_t x)
{
    return x & (x - 1);
}

/* 如果 X 是 2 的幂，则返回真，否则返回假。 */
static inline size_t
is_power_of_2(size_t x)
{
    return x != 0 && turn_off_least_1bit(x) == 0;
}

/* 每个桶的元素比率。 */
#define MIN_ELEMS_PER_BUCKET 1  /* 元素/桶 < 1：减少桶的数量。 */
#define BEST_ELEMS_PER_BUCKET 2 /* 理想的元素/桶。 */
#define MAX_ELEMS_PER_BUCKET 4  /* 元素/桶 > 4：增加桶的数量。 */

/* 改变哈希表 H 中的桶数量以匹配理想值。由于内存不足，此函数可能会失败，
   但这只会使哈希访问效率降低；我们仍然可以继续。 */
static void
rehash(struct hash *h)
{
    size_t old_bucket_cnt, new_bucket_cnt;
    struct list *new_buckets, *old_buckets;
    size_t i;

    ASSERT(h != NULL);

    /* 保存旧桶信息供以后使用。 */
    old_buckets = h->buckets;
    old_bucket_cnt = h->bucket_cnt;

    /* 计算现在要使用的桶数量。
       我们希望每 BEST_ELEMS_PER_BUCKET 个元素有一个桶。
       我们必须至少有四个桶，并且桶的数量必须是 2 的幂。 */
    new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
    if (new_bucket_cnt < 4)
        new_bucket_cnt = 4;
    while (!is_power_of_2(new_bucket_cnt))
        new_bucket_cnt = turn_off_least_1bit(new_bucket_cnt);

    /* 如果桶数量不会改变，则不做任何操作。 */
    if (new_bucket_cnt == old_bucket_cnt)
        return;

    /* 分配新桶并将其初始化为空。 */
    new_buckets = malloc(sizeof *new_buckets * new_bucket_cnt);
    if (new_buckets == NULL) {
        /* 分配失败。这意味着哈希表的使用效率将降低。
           但是，它仍然可用，所以
           没有理由认为这是一个错误。 */
        return;
    }
    for (i = 0; i < new_bucket_cnt; i++)
        list_init(&new_buckets[i]);

    /* 安装新桶信息。 */
    h->buckets = new_buckets;
    h->bucket_cnt = new_bucket_cnt;

    /* 将每个旧元素移动到适当的新桶中。 */
    for (i = 0; i < old_bucket_cnt; i++) {
        struct list *old_bucket;
        struct list_elem *elem, *next;

        old_bucket = &old_buckets[i];
        for (elem = list_begin(old_bucket);
             elem != list_end(old_bucket);
             elem = next) {
            struct list *new_bucket = find_bucket(h, list_elem_to_hash_elem(elem));
            next = list_next(elem);
            list_remove(elem);
            list_push_front(new_bucket, elem);
        }
    }

    free(old_buckets);
}

/* 将 E 插入 BUCKET（在哈希表 H 中）。 */
static void
insert_elem(struct hash *h, struct list *bucket, struct hash_elem *e)
{
    h->elem_cnt++;
    list_push_front(bucket, &e->list_elem);
}

/* 从哈希表 H 中删除 E。 */
static void
remove_elem(struct hash *h, struct hash_elem *e)
{
    h->elem_cnt--;
    list_remove(&e->list_elem);
}
