/* 文件：gdt.c
   功能：实现全局描述符表（GDT）的初始化和管理
   描述：GDT是x86架构特有的结构，定义了系统中所有进程可能使用的段
         本文件负责设置内核和用户代码段、数据段以及任务状态段(TSS) */

#include "userprog/gdt.h"

#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/tss.h"
#include <debug.h>

/* 全局变量：gdt
   功能：全局描述符表（Global Descriptor Table, GDT）数组
   描述：GDT是一个x86特有的结构，定义了系统中所有进程可能使用的段，受权限限制
         还存在一个每进程的局部描述符表（LDT），但现代操作系统不再使用
         GDT中的每个条目通过在表中的字节偏移量来识别一个段 */
static uint64_t gdt[SEL_CNT];

/* GDT辅助函数声明
   这些函数用于创建各种类型的段描述符 */
static uint64_t make_code_desc(int dpl);
static uint64_t make_data_desc(int dpl);
static uint64_t make_tss_desc(void *laddr);
static uint64_t make_gdtr_operand(uint16_t limit, void *base);

/* 函数：gdt_init
   功能：设置正确的GDT。引导加载器的GDT不包含用户模式选择子或TSS，但我们现在需要两者
   参数：无
   返回：无 */
void gdt_init(void)
{
    uint64_t gdtr_operand;

    /* 初始化GDT
       设置空段、内核代码段、内核数据段、用户代码段、用户数据段和TSS段 */
    gdt[SEL_NULL / sizeof *gdt] = 0;
    gdt[SEL_KCSEG / sizeof *gdt] = make_code_desc(0);  /* 内核代码段 */
    gdt[SEL_KDSEG / sizeof *gdt] = make_data_desc(0);  /* 内核数据段 */
    gdt[SEL_UCSEG / sizeof *gdt] = make_code_desc(3);  /* 用户代码段 */
    gdt[SEL_UDSEG / sizeof *gdt] = make_data_desc(3);  /* 用户数据段 */
    gdt[SEL_TSS / sizeof *gdt] = make_tss_desc(tss_get()); /* 任务状态段 */

    /* 加载GDTR（全局描述符表寄存器）和TR（任务寄存器）
       参见[IA32-v3a] 2.4.1 "Global Descriptor Table Register (GDTR)",
       2.4.4 "Task Register (TR)", 和 6.2.4 "Task Register" */
    gdtr_operand = make_gdtr_operand(sizeof gdt - 1, gdt);
    asm volatile("lgdt %0" : : "m"(gdtr_operand));
    asm volatile("ltr %w0" : : "q"(SEL_TSS));
}

/* 段类别：系统段或代码/数据段？ */
enum seg_class {
    CLS_SYSTEM = 0,   /* 系统段（System segment） */
    CLS_CODE_DATA = 1 /* 代码或数据段（Code or data segment） */
};

/* 段粒度：限制具有字节粒度还是4KB页粒度？ */
enum seg_granularity {
    GRAN_BYTE = 0, /* 限制具有1字节粒度（Limit has 1-byte granularity） */
    GRAN_PAGE = 1  /* 限制具有4KB粒度（Limit has 4 kB granularity） */
};

/* 函数：make_seg_desc
   功能：返回具有给定32位基址（BASE）和20位限制（LIMIT）的段描述符
         限制的解释取决于粒度（GRANULARITY）
         描述符根据类（CLASS）表示系统段或代码/数据段
         类型（TYPE）是段类型（其解释取决于类）
         段具有描述符特权级（DPL），意味着它可以在编号为DPL或更低的环中使用
         实际上，DPL==3表示用户进程可以使用该段，DPL==0表示只有内核可以使用该段
   参数：base - 段的基地址
         limit - 段的限制
         class - 段类别（系统段或代码/数据段）
         type - 段类型
         dpl - 描述符特权级
         granularity - 段粒度
   返回：64位段描述符 */
static uint64_t
make_seg_desc(uint32_t base,
              uint32_t limit,
              enum seg_class class,
              int type,
              int dpl,
              enum seg_granularity granularity)
{
    uint32_t e0, e1;

    ASSERT(limit <= 0xfffff);
    ASSERT(class == CLS_SYSTEM || class == CLS_CODE_DATA);
    ASSERT(type >= 0 && type <= 15);
    ASSERT(dpl >= 0 && dpl <= 3);
    ASSERT(granularity == GRAN_BYTE || granularity == GRAN_PAGE);

    e0 = ((limit & 0xffff) /* 限制 15:0 位 */
          | (base << 16)); /* 基址 15:0 位 */

    e1 = (((base >> 16) & 0xff)   /* 基址 23:16 位 */
          | (type << 8)           /* 段类型 */
          | (class << 12)         /* 0=系统段，1=代码/数据段 */
          | (dpl << 13)           /* 描述符特权级 */
          | (1 << 15)             /* 存在位（Present） */
          | (limit & 0xf0000)     /* 限制 16:19 位 */
          | (1 << 22)             /* 32位段 */
          | (granularity << 23)   /* 字节/页粒度 */
          | (base & 0xff000000)); /* 基址 31:24 位 */

    return e0 | ((uint64_t) e1 << 32);
}

/* 函数：make_code_desc
   功能：返回一个可读代码段的描述符，基地址为0，限制为4GB，具有给定的DPL
   参数：dpl - 描述符特权级
   返回：64位代码段描述符 */
static uint64_t
make_code_desc(int dpl)
{
    return make_seg_desc(0, 0xfffff, CLS_CODE_DATA, 10, dpl, GRAN_PAGE);
}

/* 函数：make_data_desc
   功能：返回一个可写数据段的描述符，基地址为0，限制为4GB，具有给定的DPL
   参数：dpl - 描述符特权级
   返回：64位数据段描述符 */
static uint64_t
make_data_desc(int dpl)
{
    return make_seg_desc(0, 0xfffff, CLS_CODE_DATA, 2, dpl, GRAN_PAGE);
}

/* 函数：make_tss_desc
   功能：返回一个"可用的"32位任务状态段描述符，其基地址在给定的线性地址，
         限制为0x67字节（32位TSS的大小），DPL为0
         参见[IA32-v3a] 6.2.2 "TSS Descriptor"
   参数：laddr - TSS的线性地址
   返回：64位TSS描述符 */
static uint64_t
make_tss_desc(void *laddr)
{
    return make_seg_desc((uint32_t) laddr, 0x67, CLS_SYSTEM, 9, 0, GRAN_BYTE);
}

/* 函数：make_gdtr_operand
   功能：返回一个描述符，当用作LGDT指令的操作数时产生给定的限制（LIMIT）和基址（BASE）
   参数：limit - GDT限制
         base - GDT基址
   返回：64位GDTR操作数 */
static uint64_t
make_gdtr_operand(uint16_t limit, void *base)
{
    return limit | ((uint64_t) (uint32_t) base << 16);
}
