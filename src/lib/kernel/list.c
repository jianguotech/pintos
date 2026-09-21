#include "list.h"

#include "../debug.h"

/* 我们的双向链表有两个头部元素："head"在第一个元素之前，
   "tail"在最后一个元素之后。前面头部的"prev"链接为空，
   后面头部的"next"链接也为空。它们的另外两个链接通过
   列表的内部元素相互指向。

   一个空列表看起来像这样：

                      +------+     +------+
                  <---| head |<--->| tail |--->
                      +------+     +------+

   一个有两个元素的列表看起来像这样：

        +------+     +-------+     +-------+     +------+
    <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
        +------+     +-------+     +-------+     +------+

   这种排列的对称性消除了很多列表处理中的特殊情况。例如，看看
   list_remove()：它只需要两个指针赋值，没有条件判断。这比
   没有头部元素的代码简单得多。

   (因为每个头部元素中只使用了一个指针，我们实际上可以将
   它们合并为一个头部元素，而不会影响这种简单性。但使用两个
   独立的元素使我们能够对一些操作进行一些检查，这是很有价值的。) */

static bool is_sorted(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux) UNUSED;

/* 如果ELEM是头部，返回真，否则返回假。 */
static inline bool
is_head(struct list_elem *elem)
{
    return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* 如果ELEM是内部元素，返回真，否则返回假。 */
static inline bool
is_interior(struct list_elem *elem)
{
    return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* 如果ELEM是尾部，返回真，否则返回假。 */
static inline bool
is_tail(struct list_elem *elem)
{
    return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* 将LIST初始化为空列表。 */
void list_init(struct list *list)
{
    ASSERT(list != NULL);
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

/* 返回LIST的开始位置。 */
struct list_elem *
list_begin(struct list *list)
{
    ASSERT(list != NULL);
    return list->head.next;
}

/* 返回ELEM在其列表中的下一个元素。如果ELEM是列表中的最后一个
   元素，返回列表尾部。如果ELEM本身是列表尾部，结果未定义。 */
struct list_elem *
list_next(struct list_elem *elem)
{
    ASSERT(is_head(elem) || is_interior(elem));
    return elem->next;
}

/* 返回LIST的尾部。

   list_end()常用于从前到后遍历列表。有关示例，请参见
   list.h顶部的大注释。 */
struct list_elem *
list_end(struct list *list)
{
    ASSERT(list != NULL);
    return &list->tail;
}

/* 返回LIST的反向开始位置，用于从后到前按反向顺序遍历LIST。 */
struct list_elem *
list_rbegin(struct list *list)
{
    ASSERT(list != NULL);
    return list->tail.prev;
}

/* 返回ELEM在其列表中的前一个元素。如果ELEM是列表中的第一个
   元素，返回列表头部。如果ELEM本身是列表头部，结果未定义。 */
struct list_elem *
list_prev(struct list_elem *elem)
{
    ASSERT(is_interior(elem) || is_tail(elem));
    return elem->prev;
}

/* 返回LIST的头部。

   list_rend()常用于从后到前按反向顺序遍历列表。这是典型的用法，
   按照list.h顶部示例中的方式：

      for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
           e = list_prev (e))
        {
          struct foo *f = list_entry (e, struct foo, elem);
          ...对f进行某些操作...
        }
*/
struct list_elem *
list_rend(struct list *list)
{
    ASSERT(list != NULL);
    return &list->head;
}

/* 返回LIST的头部。

   list_head()可用于遍历列表的另一种风格，例如：

      e = list_head (&list);
      while ((e = list_next (e)) != list_end (&list))
        {
          ...
        }
*/
struct list_elem *
list_head(struct list *list)
{
    ASSERT(list != NULL);
    return &list->head;
}

/* 返回LIST的尾部。 */
struct list_elem *
list_tail(struct list *list)
{
    ASSERT(list != NULL);
    return &list->tail;
}

/* 将ELEM插入BEFORE之前，BEFORE可以是内部元素或尾部。
   后一种情况等同于list_push_back()。 */
void list_insert(struct list_elem *before, struct list_elem *elem) // 把元素插到了链表头（模拟栈）
{
    ASSERT(is_interior(before) || is_tail(before));
    ASSERT(elem != NULL);

    elem->prev = before->prev;
    elem->next = before;
    before->prev->next = elem;
    before->prev = elem;
}

/* 从其当前列表中移除FIRST到LAST（不包括）的元素，然后将其
   插入BEFORE之前，BEFORE可以是内部元素或尾部。 */
void list_splice(struct list_elem *before,
                 struct list_elem *first,
                 struct list_elem *last)
{
    ASSERT(is_interior(before) || is_tail(before));
    if (first == last)
        return;
    last = list_prev(last);

    ASSERT(is_interior(first));
    ASSERT(is_interior(last));

    /* 从当前列表中干净地移除FIRST...LAST。 */
    first->prev->next = last->next;
    last->next->prev = first->prev;

    /* 将FIRST...LAST拼接到新列表中。 */
    first->prev = before->prev;
    last->next = before;
    before->prev->next = first;
    before->prev = last;
}

/* 在LIST的开始处插入ELEM，使其成为LIST的前面。 */
void list_push_front(struct list *list, struct list_elem *elem)
{
    list_insert(list_begin(list), elem);
}

/* 在LIST的末尾插入ELEM，使其成为LIST的后面。 */
void list_push_back(struct list *list, struct list_elem *elem)
{
    list_insert(list_end(list), elem);
}

/* 从其列表中移除ELEM并返回跟随它的元素。如果ELEM不在列表中，
   未定义行为。

   从列表中移除后，必须非常小心地处理列表元素。在ELEM上调用
   list_next()或list_prev()将返回之前在ELEM前面或后面的项，
   但是，例如，list_prev(list_next(ELEM))不再是ELEM！

   list_remove()返回值提供了一种从列表中迭代和移除元素的便捷方式：

   for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
     {
       ...对e进行某些操作...
     }

   如果需要free()列表的元素，则需要更谨慎一些。这是一个在这种
   情况下也有效的替代策略：

   while (!list_empty (&list))
     {
       struct list_elem *e = list_pop_front (&list);
       ...对e进行某些操作...
     }
*/
struct list_elem *
list_remove(struct list_elem *elem)
{
    ASSERT(is_interior(elem));
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    return elem->next;
}

/* 从LIST中移除前面的元素并返回它。如果LIST在移除前为空，
   未定义行为。 */
struct list_elem *
list_pop_front(struct list *list)
{
    struct list_elem *front = list_front(list);
    list_remove(front);
    return front;
}

/* 从LIST中移除后面的元素并返回它。如果LIST在移除前为空，
   未定义行为。 */
struct list_elem *
list_pop_back(struct list *list)
{
    struct list_elem *back = list_back(list);
    list_remove(back);
    return back;
}

/* 返回LIST中的前面元素。如果LIST为空，未定义行为。 */
struct list_elem *
list_front(struct list *list)
{
    ASSERT(!list_empty(list));
    return list->head.next;
}

/* 返回LIST中的后面元素。如果LIST为空，未定义行为。 */
struct list_elem *
list_back(struct list *list)
{
    ASSERT(!list_empty(list));
    return list->tail.prev;
}

/* 返回LIST中的元素数。运行时间为元素数的O(n)。 */
size_t
list_size(struct list *list)
{
    struct list_elem *e;
    size_t cnt = 0;

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        cnt++;
    return cnt;
}

/* 如果LIST为空返回真，否则返回假。 */
bool list_empty(struct list *list)
{
    return list_begin(list) == list_end(list);
}

/* 交换A和B指向的`struct list_elem *'。 */
static void
swap(struct list_elem **a, struct list_elem **b)
{
    struct list_elem *t = *a;
    *a = *b;
    *b = t;
}

/* 反转LIST的顺序。 */
void list_reverse(struct list *list)
{
    if (!list_empty(list)) {
        struct list_elem *e;

        for (e = list_begin(list); e != list_end(list); e = e->prev)
            swap(&e->prev, &e->next);
        swap(&list->head.next, &list->tail.prev);
        swap(&list->head.next->prev, &list->tail.prev->next);
    }
}

/* 仅当列表元素A到B（不包括B）根据给定辅助数据AUX的LESS
   按顺序排列时返回真。 */
static bool
is_sorted(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux)
{
    if (a != b)
        while ((a = list_next(a)) != b)
            if (less(a, list_prev(a), aux))
                return false;
    return true;
}

/* 查找一个运行，从A开始且在B之前结束，列表中的元素
   根据给定辅助数据AUX的LESS以非递减顺序排列。返回
   运行的(独占)结束。
   A到B(不包括)必须形成非空范围。 */
static struct list_elem *
find_end_of_run(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux)
{
    ASSERT(a != NULL);
    ASSERT(b != NULL);
    ASSERT(less != NULL);
    ASSERT(a != b);

    do {
        a = list_next(a);
    } while (a != b && !less(a, list_prev(a), aux));
    return a;
}

/* 将A0到A1B0(不包括)与A1B0到B1(不包括)合并，形成也在B1
   (不包括)处结束的组合范围。两个输入范围必须为非空且根据
   给定辅助数据AUX的LESS按非递减顺序排序。输出范围将以
   相同的方式排序。 */
static void
inplace_merge(struct list_elem *a0, struct list_elem *a1b0, struct list_elem *b1, list_less_func *less, void *aux)
{
    ASSERT(a0 != NULL);
    ASSERT(a1b0 != NULL);
    ASSERT(b1 != NULL);
    ASSERT(less != NULL);
    ASSERT(is_sorted(a0, a1b0, less, aux));
    ASSERT(is_sorted(a1b0, b1, less, aux));

    while (a0 != a1b0 && a1b0 != b1)
        if (!less(a1b0, a0, aux))
            a0 = list_next(a0);
        else {
            a1b0 = list_next(a1b0);
            list_splice(a0, list_prev(a1b0), a1b0);
        }
}

/* 根据给定辅助数据AUX的LESS使用自然迭代归并排序对LIST
   进行排序，运行时间为O(n lg n)，LIST中元素数的空间为O(1)。 */
void list_sort(struct list *list, list_less_func *less, void *aux)
{
    size_t output_run_cnt; /* 当前遍历中输出的运行次数。 */

    ASSERT(list != NULL);
    ASSERT(less != NULL);

    /* 反复遍历列表，合并相邻的非递减元素运行，直到只剩一个运行。 */
    do {
        struct list_elem *a0;   /* 第一个运行的开始。 */
        struct list_elem *a1b0; /* 第一个运行的结束，第二个的开始。 */
        struct list_elem *b1;   /* 第二个运行的结束。 */

        output_run_cnt = 0;
        for (a0 = list_begin(list); a0 != list_end(list); a0 = b1) {
            /* 每次迭代产生一个输出运行。 */
            output_run_cnt++;

            /* 找到两个相邻的非递减元素运行
               A0...A1B0和A1B0...B1。 */
            a1b0 = find_end_of_run(a0, list_end(list), less, aux);
            if (a1b0 == list_end(list))
                break;
            b1 = find_end_of_run(a1b0, list_end(list), less, aux);

            /* 合并运行。 */
            inplace_merge(a0, a1b0, b1, less, aux);
        }
    } while (output_run_cnt > 1);

    ASSERT(is_sorted(list_begin(list), list_end(list), less, aux));
}

/* 在LIST的正确位置插入ELEM，LIST必须根据给定辅助数据AUX
   的LESS排序。在LIST中元素数的平均情况下运行O(n)。 */
void list_insert_ordered(struct list *list, struct list_elem *elem, list_less_func *less, void *aux)
{
    struct list_elem *e;

    ASSERT(list != NULL);
    ASSERT(elem != NULL);
    ASSERT(less != NULL);

    for (e = list_begin(list); e != list_end(list); e = list_next(e))
        if (less(elem, e, aux))
            break;
    return list_insert(e, elem);
}

/* 遍历LIST并移除每组根据给定辅助数据AUX的LESS相等的相邻元素中
   除第一个外的所有元素。如果DUPLICATES非空，则LIST中的元素
   被附加到DUPLICATES。 */
void list_unique(struct list *list, struct list *duplicates, list_less_func *less, void *aux)
{
    struct list_elem *elem, *next;

    ASSERT(list != NULL);
    ASSERT(less != NULL);
    if (list_empty(list))
        return;

    elem = list_begin(list);
    while ((next = list_next(elem)) != list_end(list))
        if (!less(elem, next, aux) && !less(next, elem, aux)) {
            list_remove(next);
            if (duplicates != NULL)
                list_push_back(duplicates, next);
        }
        else
            elem = next;
}

/* 返回LIST中根据给定辅助数据AUX的LESS最大值的元素。如果有多个
   最大值，返回列表中较早出现的那个。如果列表为空，返回其尾部。 */
struct list_elem *
list_max(struct list *list, list_less_func *less, void *aux)
{
    struct list_elem *max = list_begin(list);
    if (max != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(max); e != list_end(list); e = list_next(e))
            if (less(max, e, aux))
                max = e;
    }
    return max;
}

/* 返回LIST中根据给定辅助数据AUX的LESS最小值的元素。如果有多个
   最小值，返回列表中较早出现的那个。如果列表为空，返回其尾部。 */
struct list_elem *
list_min(struct list *list, list_less_func *less, void *aux)
{
    struct list_elem *min = list_begin(list);
    if (min != list_end(list)) {
        struct list_elem *e;

        for (e = list_next(min); e != list_end(list); e = list_next(e))
            if (less(e, min, aux))
                min = e;
    }
    return min;
}
