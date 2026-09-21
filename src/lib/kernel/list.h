#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 双向链表。

   这个双向链表的实现不需要动态分配内存。相反，每个可能作为列表元素的
   结构体必须嵌入一个 struct list_elem 成员。所有列表函数都对这些
   `struct list_elem' 进行操作。list_entry 宏允许从 struct list_elem
   转换回包含它的结构体对象。

   例如，假设需要一个 `struct foo' 的列表。`struct foo' 应该包含一个
   `struct list_elem' 成员，如下所示：

      struct foo
        {
          struct list_elem elem;
          int bar;
          ...other members...
        };

   然后可以声明并初始化一个 `struct foo' 的列表，如下所示：

      struct list foo_list;

      list_init (&foo_list);

   迭代是一个典型的情况，需要从 struct list_elem 转换回其包含的结构体。
   下面是使用 foo_list 的一个例子：

      struct list_elem *e;

      for (e = list_begin (&foo_list); e != list_end (&foo_list);
           e = list_next (e))
        {
          struct foo *f = list_entry (e, struct foo, elem);
          ...do something with f...
        }

   你可以在源代码中找到链表的真实使用例子；例如，threads 目录中的
   malloc.c、palloc.c 和 thread.c 都使用了链表。

   这个链表的接口灵感来自 C++ STL 中的 list<> 模板。如果你熟悉
   list<>，你会发现这很容易使用。但是需要强调的是，这些链表不进行
   类型检查，无法进行太多其他正确性检查。如果你出错，它会导致问题。

   链表术语术语表：

     - "front"：链表中的第一个元素。在空链表中未定义。由
       list_front() 返回。

     - "back"：链表中的最后一个元素。在空链表中未定义。由
       list_back() 返回。

     - "tail"：概念上位于链表最后一个元素之后的元素。即使在空链表中
       也是良好定义的。由 list_end() 返回。用作从前到后迭代的结束
       哨兵。

     - "beginning"：在非空链表中为前端。在空链表中为尾部。由
       list_begin() 返回。用作从前到后迭代的起点。

     - "head"：概念上位于链表第一个元素之前的元素。即使在空链表中
       也是良好定义的。由 list_rend() 返回。用作从后到前迭代的结束
       哨兵。

     - "reverse beginning"：在非空链表中为后端。在空链表中为头部。由
       list_rbegin() 返回。用作从后到前迭代的起点。

     - "interior element"：不是头部或尾部的元素，即真实的链表元素。
       空链表没有任何内部元素。
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 链表元素。 */
struct list_elem {
    struct list_elem *prev; /* 前驱链表元素。 */
    struct list_elem *next; /* 后继链表元素。 */
};

/* 链表。 */
struct list {
    struct list_elem head; /* 链表头。 */
    struct list_elem tail; /* 链表尾。 */
};

/* 将指向链表元素 LIST_ELEM 的指针转换为包含 LIST_ELEM 的结构体的指针。
   提供外部结构体 STRUCT 的名称和链表元素的成员名 MEMBER。
   有关示例，请参阅文件顶部的大注释。 */
#define list_entry(LIST_ELEM, STRUCT, MEMBER) \
    ((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next - offsetof(STRUCT, MEMBER.next)))

/* 链表初始化。

   可以通过调用 list_init() 来初始化链表：

       struct list my_list;
       list_init (&my_list);

   或者使用 LIST_INITIALIZER 初始化器：

       struct list my_list = LIST_INITIALIZER (my_list); */
#define LIST_INITIALIZER(NAME) \
    {                          \
        {NULL, &(NAME).tail},  \
        {                      \
            &(NAME).head, NULL \
        }                      \
    }

void list_init(struct list *);

/* 链表遍历。 */
/* 函数：list_begin
   功能：获取链表开始位置（第一个元素或尾部）
   参数：list - 要操作的链表指针
   返回：链表开始位置的元素指针 */
struct list_elem *list_begin(struct list *);

/* 函数：list_next
   功能：获取链表中下一个元素
   参数：elem - 当前元素指针
   返回：下一个元素指针 */
struct list_elem *list_next(struct list_elem *);

/* 函数：list_end
   功能：获取链表结束位置（尾部哨兵）
   参数：list - 要操作的链表指针
   返回：链表尾部哨兵元素指针 */
struct list_elem *list_end(struct list *);

/* 函数：list_rbegin
   功能：获取链表反向开始位置（最后一个元素或头部）
   参数：list - 要操作的链表指针
   返回：链表反向开始位置的元素指针 */
struct list_elem *list_rbegin(struct list *);

/* 函数：list_prev
   功能：获取链表中前一个元素
   参数：elem - 当前元素指针
   返回：前一个元素指针 */
struct list_elem *list_prev(struct list_elem *);

/* 函数：list_rend
   功能：获取链表反向结束位置（头部哨兵）
   参数：list - 要操作的链表指针
   返回：链表头部哨兵元素指针 */
struct list_elem *list_rend(struct list *);

/* 函数：list_head
   功能：获取链表头部哨兵元素
   参数：list - 要操作的链表指针
   返回：链表头部哨兵元素指针 */
struct list_elem *list_head(struct list *);

/* 函数：list_tail
   功能：获取链表尾部哨兵元素
   参数：list - 要操作的链表指针
   返回：链表尾部哨兵元素指针 */
struct list_elem *list_tail(struct list *);

/* 链表插入。 */
/* 函数：list_insert
   功能：在指定元素之前插入新元素
   参数：before - 插入位置的目标元素，new_elem - 要插入的新元素
   返回：无 */
void list_insert(struct list_elem *, struct list_elem *);

/* 函数：list_splice
   功能：将一个元素序列插入到指定位置之前
   参数：before - 插入位置的目标元素，first - 要插入序列的第一个元素，last - 要插入序列的最后一个元素的下一个
   返回：无 */
void list_splice(struct list_elem *before,
                 struct list_elem *first,
                 struct list_elem *last);

/* 函数：list_push_front
   功能：在链表头部插入元素
   参数：list - 要操作的链表指针，elem - 要插入的元素
   返回：无 */
void list_push_front(struct list *, struct list_elem *);

/* 函数：list_push_back
   功能：在链表尾部插入元素
   参数：list - 要操作的链表指针，elem - 要插入的元素
   返回：无 */
void list_push_back(struct list *, struct list_elem *);

/* 链表移除。 */
/* 函数：list_remove
   功能：从链表中移除指定元素
   参数：elem - 要移除的元素
   返回：被移除元素的下一个元素指针 */
struct list_elem *list_remove(struct list_elem *);

/* 函数：list_pop_front
   功能：移除并返回链表头部元素
   参数：list - 要操作的链表指针
   返回：被移除的头部元素指针 */
struct list_elem *list_pop_front(struct list *);

/* 函数：list_pop_back
   功能：移除并返回链表尾部元素
   参数：list - 要操作的链表指针
   返回：被移除的尾部元素指针 */
struct list_elem *list_pop_back(struct list *);

/* 链表元素。 */
/* 函数：list_front
   功能：获取链表第一个元素
   参数：list - 要操作的链表指针
   返回：链表第一个元素指针，空链表时未定义 */
struct list_elem *list_front(struct list *);

/* 函数：list_back
   功能：获取链表最后一个元素
   参数：list - 要操作的链表指针
   返回：链表最后一个元素指针，空链表时未定义 */
struct list_elem *list_back(struct list *);

/* 链表属性。 */
/* 函数：list_size
   功能：获取链表中元素的数量
   参数：list - 要操作的链表指针
   返回：链表中元素的个数 */
size_t list_size(struct list *);

/* 函数：list_empty
   功能：检查链表是否为空
   参数：list - 要操作的链表指针
   返回：链表为空时返回true，否则返回false */
bool list_empty(struct list *);

/* 杂项。 */
/* 函数：list_reverse
   功能：反转链表中元素的顺序
   参数：list - 要操作的链表指针
   返回：无 */
void list_reverse(struct list *);

/* 比较两个链表元素 A 和 B 的值，给定辅助数据 AUX。如果 A 小于 B
   返回真，如果 A 大于或等于 B 返回假。 */
typedef bool list_less_func(const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux);

/* 对有序元素的链表进行操作。 */
/* 函数：list_sort
   功能：对链表进行排序
   参数：list - 要排序的链表指针，less - 比较函数指针，aux - 辅助数据
   返回：无 */
void list_sort(struct list *,
               list_less_func *,
               void *aux);

/* 函数：list_insert_ordered
   功能：按顺序插入元素到链表中
   参数：list - 要操作的链表指针，elem - 要插入的元素，less - 比较函数指针，aux - 辅助数据
   返回：无 */
void list_insert_ordered(struct list *, struct list_elem *, list_less_func *, void *aux);

/* 函数：list_unique
   功能：移除链表中的重复元素，保留不重复的元素
   参数：list - 要操作的链表指针，duplicates - 存储重复元素的链表，less - 比较函数指针，aux - 辅助数据
   返回：无 */
void list_unique(struct list *, struct list *duplicates, list_less_func *, void *aux);

/* 最大值和最小值。 */
/* 函数：list_max
   功能：找到链表中的最大元素
   参数：list - 要操作的链表指针，less - 比较函数指针，aux - 辅助数据
   返回：链表中最大元素的指针 */
struct list_elem *list_max(struct list *, list_less_func *, void *aux);

/* 函数：list_min
   功能：找到链表中的最小元素
   参数：list - 要操作的链表指针，less - 比较函数指针，aux - 辅助数据
   返回：链表中最小元素的指针 */
struct list_elem *list_min(struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
