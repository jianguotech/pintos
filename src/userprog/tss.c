/* 文件：tss.c
   功能：实现任务状态段（TSS）的管理
   描述：本文件实现了x86架构特有的任务状态段结构管理
         主要用于处理用户态到内核态的中断栈切换机制 */

#include "userprog/tss.h"

#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include <debug.h>
#include <stddef.h>

/* 任务状态段（Task-State Segment, TSS）

   TSS是x86特有的结构，用于定义"任务"，这是处理器内置的多任务支持形式
   然而，出于各种原因，包括可移植性、速度和灵活性，大多数x86操作系统几乎完全忽略TSS
   我们也不例外

   不幸的是，有一件事只能使用TSS来完成：用户模式下发生中断时的栈切换
   当中断在用户模式（ring 3）发生时，处理器会查看当前TSS的ss0和esp0成员
   以确定用于处理中断的栈。因此，我们必须创建一个TSS并至少初始化这些字段
   这正是本文件要做的事情

   当中断被中断或陷阱门处理时（这适用于我们处理的所有中断），x86处理器工作如下：

     - 如果被中断的代码与中断处理程序在相同的环中，则不发生栈切换
       这是我们在内核中运行时发生中断的情况
       对于这种情况，TSS的内容是无关紧要的

     - 如果被中断的代码与处理程序在不同的环中，则处理器切换到TSS中为新环指定的栈
       这是我们在用户空间中发生中断的情况
       切换到一个尚未使用的栈以避免损坏是很重要的
       因为我们在用户空间中运行，所以知道当前进程的内核栈未被使用
       因此我们总是可以使用它
       因此，当调度器切换线程时，它也会将TSS的栈指针更改为指向新线程的内核栈
       （调用在thread.c中的thread_schedule_tail()中）

   参见[IA32-v3a] 6.2.1 "Task-State Segment (TSS)"了解TSS的描述
   参见[IA32-v3a] 5.12.1 "Exception- or Interrupt-Handler Procedures"了解中断期间
   何时以及如何发生栈切换的描述 */
struct tss {
    uint16_t back_link, : 16;
    void *esp0;         /* Ring 0 栈虚拟地址 */
    uint16_t ss0, : 16; /* Ring 0 栈段选择子 */
    void *esp1;
    uint16_t ss1, : 16;
    void *esp2;
    uint16_t ss2, : 16;
    uint32_t cr3;
    void (*eip)(void);
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint16_t es, : 16;
    uint16_t cs, : 16;
    uint16_t ss, : 16;
    uint16_t ds, : 16;
    uint16_t fs, : 16;
    uint16_t gs, : 16;
    uint16_t ldt, : 16;
    uint16_t trace, bitmap;
};

/* 内核TSS */
static struct tss *tss;

/* 函数：tss_init
   功能：初始化内核任务状态段（TSS）
   参数：无
   返回：无 */
void tss_init(void)
{
    /* 我们的TSS从不用于调用门或任务门，所以只有几个字段被引用
       而且这些字段仅是我们初始化的字段 */
    tss = palloc_get_page(PAL_ASSERT | PAL_ZERO);
    tss->ss0 = SEL_KDSEG;
    tss->bitmap = 0xdfff;
    tss_update();
}

/* 函数：tss_get
   功能：返回内核任务状态段（TSS）的指针
   参数：无
   返回：TSS结构指针 */
struct tss *
tss_get(void)
{
    ASSERT(tss != NULL);
    return tss;
}

/* 函数：tss_update
   功能：将TSS中的ring 0栈指针设置为指向线程栈的末尾
   参数：无
   返回：无 */
void tss_update(void)
{
    ASSERT(tss != NULL);
    /* 设置内核栈指针为当前线程栈的顶部 */
    tss->esp0 = (uint8_t *) thread_current() + PGSIZE;
}