#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

/* 由 PC BIOS 固定的常量。 */
#define LOADER_BASE 0x7c00 /* 加载器基地址的物理地址。 */
#define LOADER_END 0x7e00  /* 加载器结束地址的物理地址。 */

/* 内核基地址的物理地址。 */
#define LOADER_KERN_BASE 0x20000 /* 128 kB. */

/* 映射所有物理内存的内核虚拟地址。
   必须在 4 MB 边界上对齐。 */
#define LOADER_PHYS_BASE 0xc0000000 /* 3 GB. */

/* 重要的加载器物理地址。 */
#define LOADER_SIG (LOADER_END - LOADER_SIG_LEN)          /* 0xaa55 BIOS 签名。 */
#define LOADER_PARTS (LOADER_SIG - LOADER_PARTS_LEN)      /* 分区表。 */
#define LOADER_ARGS (LOADER_PARTS - LOADER_ARGS_LEN)      /* 命令行参数。 */
#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* 参数数量。 */

/* 加载器数据结构的大小。 */
#define LOADER_SIG_LEN 2
#define LOADER_PARTS_LEN 64
#define LOADER_ARGS_LEN 128
#define LOADER_ARG_CNT_LEN 4

/* 由加载器定义的 GDT 选择子。
   更多选择子在 userprog/gdt.h 中定义。 */
#define SEL_NULL 0x00  /* 空选择子。 */
#define SEL_KCSEG 0x08 /* 内核代码选择子。 */
#define SEL_KDSEG 0x10 /* 内核数据选择子。 */

#ifndef __ASSEMBLER__
#include <stdint.h>

/* 物理内存总量，单位为 4 kB 页。 */
extern uint32_t init_ram_pages;
#endif

#endif /* threads/loader.h */
