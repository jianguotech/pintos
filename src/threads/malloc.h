#ifndef THREADS_MALLOC_H
#define THREADS_MALLOC_H

#include <debug.h>
#include <stddef.h>

/* 函数：malloc_init
   功能：初始化内存分配器的描述符表
   参数：无
   返回：无 */
void malloc_init(void);
/* 函数：malloc
   功能：分配至少 size 字节的堆内存，成功返回指针，失败返回 NULL
   参数：size - 要分配的字节数
   返回：分配的内存指针，失败返回 NULL */
void *malloc(size_t) __attribute__((malloc));
/* 函数：calloc
   功能：分配 a*b 字节并清零，返回指针或 NULL
   参数：a - 元素个数，b - 每个元素的大小
   返回：分配并清零的内存指针，失败返回 NULL */
void *calloc(size_t, size_t) __attribute__((malloc));
/* 函数：realloc
   功能：调整先前分配块的大小，可能返回新的地址或 NULL
   参数：ptr - 先前分配的内存指针，size - 新的大小
   返回：调整后的内存指针，失败返回 NULL */
void *realloc(void *, size_t);
/* 函数：free
   功能：释放先前由 malloc/calloc/realloc 分配的内存块
   参数：ptr - 要释放的内存指针
   返回：无 */
void free(void *);

#endif /* threads/malloc.h */
