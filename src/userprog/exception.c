/* 文件：exception.c
   功能：处理用户程序产生的异常和中断
   描述：本文件实现了对各种异常的处理程序，包括除零错误、页面错误等
         当用户程序执行非法操作时，这些处理程序会被调用 */

#include "userprog/exception.h"

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include <inttypes.h>
#include <stdio.h>

/* 全局变量：page_fault_cnt
   功能：页面错误计数器，记录处理的页面错误总数，用于统计和调试 */
static long long page_fault_cnt;

/* 内部函数声明
   功能：这些函数处理不同类型的异常 */
static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/* 函数：exception_init
   功能：为可能由用户程序引起的中断注册处理程序
         在真正的类Unix操作系统中，大多数这些中断会以信号的形式传递给用户进程
         如[SV-386] 3-24和3-25所述，但我们不实现信号
         相反，我们将让它们简单地杀死用户进程
         页面错误是一个例外，在这里它们被处理为与其他异常相同的方式
         但要实现虚拟内存，这需要改变
         参见[IA32-v3a] 5.15 "Exception and Interrupt Reference"了解每个异常的描述
   参数：无
   返回：无 */
void exception_init(void)
{
    /* 这些异常可以由用户程序显式引发，例如通过INT、INT3、INTO和BOUND指令
       因此，我们设置DPL==3，意味着允许用户程序通过这些指令调用它们 */
    intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
    intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
    intr_register_int(5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

    /* 这些异常的DPL==0，防止用户进程通过INT指令调用它们
       它们仍然可以被间接引起，例如#DE可以通过除以0引起 */
    intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
    intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
    intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
    intr_register_int(7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
    intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
    intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
    intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
    intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
    intr_register_int(19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

    /* 大多数异常可以在中断开启的情况下处理
       我们需要为页面错误禁用中断，因为错误地址存储在CR2中并且需要保留 */
    intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 函数：exception_print_stats
   功能：打印异常统计信息（页面错误计数）
   参数：无
   返回：无 */
void exception_print_stats(void)
{
    printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* 函数：kill
   功能：处理可能由用户进程引起的异常的处理程序
   参数：f - 中断帧指针，包含异常发生时的CPU状态
   返回：无 */
static void
kill(struct intr_frame *f)
{
    /* 这个中断（可能）是由用户进程引起的
       例如，进程可能尝试访问未映射的虚拟内存（页面错误）
       目前，我们简单地杀死用户进程
       稍后，我们希望在内核中处理页面错误
       真正的类Unix操作系统通过信号将大多数异常传递回进程，但我们不实现信号 */

    /* 中断帧的代码段值告诉我们异常起源于哪里 */
    switch (f->cs) {
    case SEL_UCSEG:
        /* 用户代码段，所以这是用户异常，正如我们所期望的
           杀死用户进程 */
        printf("%s: dying due to interrupt %#04x (%s).\n",
               thread_name(),
               f->vec_no,
               intr_name(f->vec_no));
        intr_dump_frame(f);
        thread_exit();

    case SEL_KCSEG:
        /* 内核代码段，这表示内核错误
           内核代码不应该抛出异常（页面错误可能导致内核异常--但它们不应该到达这里）
           内核恐慌以表明问题 */
        intr_dump_frame(f);
        PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
        /* 其他代码段？不应该发生
           内核恐慌 */
        printf("Interrupt %#04x (%s) in unknown segment %04x\n",
               f->vec_no,
               intr_name(f->vec_no),
               f->cs);
        thread_exit();
    }
}

/* 函数：page_fault
   功能：页面错误处理程序，这是实现虚拟内存必须填充的框架
         项目2的一些解决方案可能还需要修改此代码
         在入口处，出错的地址在CR2（控制寄存器2）中
         关于错误的信息，格式如exception.h中的PF_*宏所述，在f的error_code成员中
         这里的示例代码显示了如何解析该信息
         您可以在[IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception (#PF)"中
         的描述中找到有关这两者的更多信息
   参数：f - 中断帧指针
   返回：无 */
static void
page_fault(struct intr_frame *f)
{
    bool not_present; /* 真：页不存在，假：写入只读页 */
    bool write;       /* 真：访问是写，假：访问是读 */
    bool user;        /* 真：用户访问，假：内核访问 */
    void *fault_addr; /* 错误地址 */

    /* 获取出错的地址，即导致错误的访问的虚拟地址
       它可能指向代码或数据
       它不一定是导致错误的指令地址（那是f->eip）
       参见[IA32-v2a] "MOV--Move to/from Control Registers"和
       [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception (#PF)" */
    asm("movl %%cr2, %0" : "=r"(fault_addr));

    /* 重新开启中断（它们只是关闭，以便我们可以确保在CR2更改之前读取它） */
    intr_enable();

    /* 计算页面错误 */
    page_fault_cnt++;

    /* 确定原因 */
    not_present = (f->error_code & PF_P) == 0;
    write = (f->error_code & PF_W) != 0;
    user = (f->error_code & PF_U) != 0;

    /* 要实现虚拟内存，请删除函数体的其余部分，
       并用引入fault_addr引用的页面的代码替换它 */
    printf("Page fault at %p: %s error %s page in %s context.\n",
           fault_addr,
           not_present ? "not present" : "rights violation",
           write ? "writing" : "reading",
           user ? "user" : "kernel");
    kill(f);
}
