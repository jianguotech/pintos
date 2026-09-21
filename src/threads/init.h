#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 全局指针：仅含内核映射的初始页目录，在启动阶段由 paging_init 构建。 */
extern uint32_t *init_page_dir;

#endif /* threads/init.h */
