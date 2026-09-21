#include "threads/interrupt.h"

#include "devices/timer.h"
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* 可编程中断控制器（PIC）寄存器地址，PC 采用主从级联结构（从片挂在主片 IRQ2）。 */
#define PIC0_CTRL 0x20 /* 主片控制寄存器。 */
#define PIC0_DATA 0x21 /* 主片数据寄存器。 */
#define PIC1_CTRL 0xa0 /* 从片控制寄存器。 */
#define PIC1_DATA 0xa1 /* 从片数据寄存器。 */

/* x86 支持的中断向量总数。 */
#define INTR_CNT 256

/* 中断描述符表（IDT），格式由 CPU 规定，详见 IA32 手册相关章节。 */
static uint64_t idt[INTR_CNT];

/* 各向量对应的处理函数指针。 */
static intr_handler_func *intr_handlers[INTR_CNT];

/* 每个向量的调试名称。 */
static const char *intr_names[INTR_CNT];

/* 无处理例程的“意外中断”计数。 */
static unsigned int unexpected_cnt[INTR_CNT];

/* 外部中断由设备触发，运行期间禁用嵌套与睡眠，可通过 intr_yield_on_return() 请求调度。 */
static bool in_external_intr; /* 是否处于外部中断处理。 */
static bool yield_on_return;  /* 中断返回前是否触发调度。 */

/* PIC 辅助函数。 */
static void pic_init(void);
static void pic_end_of_interrupt(int irq);

/* IDT 辅助函数。 */
static uint64_t make_intr_gate(void (*)(void), int dpl);
static uint64_t make_trap_gate(void (*)(void), int dpl);
static inline uint64_t make_idtr_operand(uint16_t limit, void *base);

/* 中断处理入口。 */
void intr_handler(struct intr_frame *args);
static void unexpected_interrupt(const struct intr_frame *);

/* 函数：intr_get_level
   功能：读取当前 CPU 的中断使能状态。 */
enum intr_level
intr_get_level(void)
{
    uint32_t flags;

    /* 将 EFLAGS 压入栈并弹出到 `flags' 变量以读取当前的标志位。
       参考：[IA32-v2b] "PUSHF/POP" 和 [IA32-v3a] 5.8.1 关于屏蔽可屏蔽
       中断的章节。 */
    asm volatile("pushfl; popl %0" : "=g"(flags));

    return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* 函数：intr_set_level
   功能：根据 level 启用或禁用中断，并返回旧状态。 */
enum intr_level
intr_set_level(enum intr_level level)
{
    return level == INTR_ON ? intr_enable() : intr_disable();
}

/* 函数：intr_enable
   功能：开启中断，返回开启前的状态。 */
enum intr_level
intr_enable(void)
{
    enum intr_level old_level = intr_get_level();
    ASSERT(!intr_context());

    /* 使用 STI 指令设置 IF 位以开启中断。 */
    asm volatile("sti");

    return old_level;
}

/* 函数：intr_disable
   功能：关闭中断，返回关闭前的状态。 */
enum intr_level
intr_disable(void)
{
    enum intr_level old_level = intr_get_level();

    /* 使用 CLI 指令清除 IF 位以关闭中断。 */
    asm volatile("cli" : : : "memory");

    return old_level;
}

/* 函数：intr_init
   功能：初始化中断控制器与 IDT 表，建立基础中断环境。 */
void intr_init(void)
{
    uint64_t idtr_operand;
    int i;

    /* 初始化可编程中断控制器（PIC）。 */
    pic_init();

    /* 初始化中断描述符表（IDT）。 */
    for (i = 0; i < INTR_CNT; i++)
        idt[i] = make_intr_gate(intr_stubs[i], 0);

    /* 使用 lidt 指令加载 IDT 寄存器。 */
    idtr_operand = make_idtr_operand(sizeof idt - 1, idt);
    asm volatile("lidt %0" : : "m"(idtr_operand));

    /* 填充默认中断名称表，用于调试输出。 */
    for (i = 0; i < INTR_CNT; i++)
        intr_names[i] = "unknown";
    intr_names[0] = "#DE Divide Error";
    intr_names[1] = "#DB Debug Exception";
    intr_names[2] = "NMI Interrupt";
    intr_names[3] = "#BP Breakpoint Exception";
    intr_names[4] = "#OF Overflow Exception";
    intr_names[5] = "#BR BOUND Range Exceeded Exception";
    intr_names[6] = "#UD Invalid Opcode Exception";
    intr_names[7] = "#NM Device Not Available Exception";
    intr_names[8] = "#DF Double Fault Exception";
    intr_names[9] = "Coprocessor Segment Overrun";
    intr_names[10] = "#TS Invalid TSS Exception";
    intr_names[11] = "#NP Segment Not Present";
    intr_names[12] = "#SS Stack Fault Exception";
    intr_names[13] = "#GP General Protection Exception";
    intr_names[14] = "#PF Page-Fault Exception";
    intr_names[16] = "#MF x87 FPU Floating-Point Error";
    intr_names[17] = "#AC Alignment Check Exception";
    intr_names[18] = "#MC Machine-Check Exception";
    intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/* 函数：register_handler
   功能：为给定中断向量安装处理例程，同时根据需求设置陷阱门或中断门以及 DPL。
   参数：vec_no —— 中断向量号；dpl —— 描述符特权级；level —— 处理时的中断开关；
         handler —— 实际处理函数；name —— 调试使用的名称。
   返回：无。 */
static void
register_handler(uint8_t vec_no, int dpl, enum intr_level level, intr_handler_func *handler, const char *name)
{
    ASSERT(intr_handlers[vec_no] == NULL);
    if (level == INTR_ON)
        idt[vec_no] = make_trap_gate(intr_stubs[vec_no], dpl);
    else
        idt[vec_no] = make_intr_gate(intr_stubs[vec_no], dpl);
    intr_handlers[vec_no] = handler;
    intr_names[vec_no] = name;
}

/* 函数：intr_register_ext
   功能：注册外部设备中断，默认以中断门形式运行并禁止嵌套。
   参数：vec_no —— 外部中断向量号；handler —— 设备处理函数；name —— 中断名称。
   返回：无。 */
void intr_register_ext(uint8_t vec_no, intr_handler_func *handler, const char *name)
{
    ASSERT(vec_no >= 0x20 && vec_no <= 0x2f);
    register_handler(vec_no, 0, INTR_OFF, handler, name);
}

/* 函数：intr_register_int
   功能：注册内部中断或异常，可按需选择陷阱门或中断门以及调用权限。
   参数：vec_no —— 中断向量号；dpl —— 调用者所需特权级；level —— 运行时中断状态；
         handler —— 对应处理函数；name —— 标识名称。
   返回：无。 */
void intr_register_int(uint8_t vec_no, int dpl, enum intr_level level, intr_handler_func *handler, const char *name)
{
    ASSERT(vec_no < 0x20 || vec_no > 0x2f);
    register_handler(vec_no, dpl, level, handler, name);
}

/* 函数：intr_context
   功能：判断当前是否处于外部中断上下文。 */
bool intr_context(void)
{
    return in_external_intr;
}

/* 函数：intr_yield_on_return
   功能：在外部中断返回前请求调度器让出当前线程。 */
void intr_yield_on_return(void)
{
    ASSERT(intr_context());
    yield_on_return = true;
}

/* 8259A 可编程中断控制器（PIC）相关实现。 */

/* 函数：pic_init
   功能：初始化主/从 PIC，并将外部中断重映射到向量 0x20-0x2f。

   说明：默认情况下 PIC 将 IRQ0..IRQ15 映射到中断向量 0..15，这些
   向量与 CPU 异常向量冲突，因此我们将其重映射为 32..47 (0x20..0x2f)。 */
static void
pic_init(void)
{
    /* 屏蔽主/从 PIC 的所有中断。 */
    outb(PIC0_DATA, 0xff);
    outb(PIC1_DATA, 0xff);

    /* 初始化主 PIC。 */
    outb(PIC0_CTRL, 0x11); /* ICW1：单片模式，边沿触发，期望后续 ICW4。 */
    outb(PIC0_DATA, 0x20); /* ICW2：将 IR0..7 映射到向量 0x20..0x27。 */
    outb(PIC0_DATA, 0x04); /* ICW3：从片挂接在 IR2。 */
    outb(PIC0_DATA, 0x01); /* ICW4：8086 模式，正常 EOI，非缓冲。 */

    /* 初始化从 PIC。 */
    outb(PIC1_CTRL, 0x11); /* ICW1：单片模式，边沿触发，期望后续 ICW4。 */
    outb(PIC1_DATA, 0x28); /* ICW2：将 IR0..7 映射到向量 0x28..0x2f。 */
    outb(PIC1_DATA, 0x02); /* ICW3：从片在主片的 IR2 上。 */
    outb(PIC1_DATA, 0x01); /* ICW4：8086 模式，正常 EOI，非缓冲。 */

    /* 取消屏蔽所有中断。 */
    outb(PIC0_DATA, 0x00);
    outb(PIC1_DATA, 0x00);
}

/* 函数：pic_end_of_interrupt
   功能：向 PIC 发送中断结束信号，允许下一次同类中断到达。 */
/* 向 PIC 发送中断结束（EOI）信号。
   若不确认 IRQ，相关中断将不会再次到达，因此必须发送 EOI。 */
static void
pic_end_of_interrupt(int irq)
{
    ASSERT(irq >= 0x20 && irq < 0x30);

    /* 向主 PIC 发送确认（EOI）。 */
    outb(0x20, 0x20);

    /* 若为从片中断，则向从片发送确认（EOI）。 */
    if (irq >= 0x28)
        outb(0xa0, 0x20);
}

/* 构造一个 IDT 门描述符以调用 FUNCTION。

   DPL 指定描述符的特权级（Descriptor Privilege Level），
   DPL==3 允许用户态调用，DPL==0 则仅内核可调用。用户态发生的
   异常仍然会触发 DPL==0 的门。

   TYPE 可以是 14（中断门）或 15（陷阱门）。二者差别在于：
   进入中断门时 CPU 会禁用中断（IF 清零），而进入陷阱门则
   保持当前中断状态。参考 IA32 手册相关章节了解详细语义。 */
/* 函数：make_gate
   功能：构造 IDT 描述符，区分中断门与陷阱门并设置特权级。 */
static uint64_t
make_gate(void (*function)(void), int dpl, int type)
{
    uint32_t e0, e1;

    ASSERT(function != NULL);
    ASSERT(dpl >= 0 && dpl <= 3);
    ASSERT(type >= 0 && type <= 15);

    e0 = (((uint32_t) function & 0xffff) /* 偏移位 15:0。 */
          | (SEL_KCSEG << 16));          /* 目标代码段选择子。 */

    e1 = (((uint32_t) function & 0xffff0000) /* 偏移位 31:16。 */
          | (1 << 15)                        /* 存在位（Present）。 */
          | ((uint32_t) dpl << 13)           /* 描述符特权级（DPL）。 */
          | (0 << 12)                        /* 系统位。 */
          | ((uint32_t) type << 8));         /* 门类型（Gate type）。 */

    return e0 | ((uint64_t) e1 << 32);
}

/* 生成一个中断门（interrupt gate），当被触发时会调用 FUNCTION，
   并按 DPL 指定允许的调用特权级。 */
/* 函数：make_intr_gate
   功能：生成中断门描述符，进入后自动屏蔽中断。 */
static uint64_t
make_intr_gate(void (*function)(void), int dpl)
{
    return make_gate(function, dpl, 14);
}

/* 生成一个陷阱门（trap gate），当被触发时会调用 FUNCTION，
   并按 DPL 指定允许的调用特权级；与中断门不同，进入陷阱门
   不会自动屏蔽中断。 */
/* 函数：make_trap_gate
   功能：生成陷阱门描述符，进入后保持当前中断状态。 */
static uint64_t
make_trap_gate(void (*function)(void), int dpl)
{
    return make_gate(function, dpl, 15);
}

/* 返回一个可作为 LIDT 操作数的描述符，其包含 LIMIT 与 BASE。
   该描述在加载 IDT 时被使用。 */
/* 函数：make_idtr_operand
   功能：打包 lidt 指令需要的界限和基址。 */
static inline uint64_t
make_idtr_operand(uint16_t limit, void *base)
{
    return limit | ((uint64_t) (uint32_t) base << 16);
}

/* 中断处理例程。 */

/* 统一的中断处理入口。由汇编桩（intr-stubs.S）调用。
   参数 FRAME 描述了触发的中断向量及被打断线程的寄存器状态。 */
/* 函数：intr_handler
   功能：统一的中断入口，根据向量号分派到注册的处理函数并
         完成外部中断的收尾工作。 */
void intr_handler(struct intr_frame *frame)
{
    bool external;
    intr_handler_func *handler;

    /* 外部中断的特殊性：
       我们一次只处理一个外部中断（因此进入此处理时中断必须关闭），
       并且必须向 PIC 发送确认（见下文）。外部中断处理函数不能睡眠。 */
    external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
    if (external) {
        ASSERT(intr_get_level() == INTR_OFF);
        ASSERT(!intr_context());

        in_external_intr = true;
        yield_on_return = false;
    }

    /* 调用对应中断向量的处理例程。 */
    handler = intr_handlers[frame->vec_no];
    if (handler != NULL)
        handler(frame);
    else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
        /* 该向量偶尔因硬件噪声或竞争产生伪中断，直接忽略。 */
    }
    else
        unexpected_interrupt(frame);

    /* 外部中断收尾。 */
    if (external) {
        ASSERT(intr_get_level() == INTR_OFF);
        ASSERT(intr_context());

        in_external_intr = false;
        pic_end_of_interrupt(frame->vec_no);

        if (yield_on_return)
            thread_yield();
    }
}

/* 处理意外的中断（未注册处理函数）。
   该函数统计触发次数并在适当频率下打印调试信息。 */
/* 函数：unexpected_interrupt
   功能：统计并在必要时报告未注册处理例程的中断。 */
static void
unexpected_interrupt(const struct intr_frame *f)
{
    /* 累计触发次数。 */
    unsigned int n = ++unexpected_cnt[f->vec_no];

    /* 若次数为 2 的幂则打印日志，控制输出频率。 */
    if ((n & (n - 1)) == 0)
        printf("Unexpected interrupt %#04x (%s)\n",
               f->vec_no,
               intr_names[f->vec_no]);
}

/* 将中断帧内容输出到控制台便于调试。 */
/* 函数：intr_dump_frame
   功能：打印中断帧寄存器状态，便于调试。 */
void intr_dump_frame(const struct intr_frame *f)
{
    uint32_t cr2;

    /* 读取 CR2（最近一次缺页异常的线性地址）。 */
    asm("movl %%cr2, %0" : "=r"(cr2));

    printf("Interrupt %#04x (%s) at eip=%p\n",
           f->vec_no,
           intr_names[f->vec_no],
           f->eip);
    printf(" cr2=%08" PRIx32 " error=%08" PRIx32 "\n", cr2, f->error_code);
    printf(" eax=%08" PRIx32 " ebx=%08" PRIx32 " ecx=%08" PRIx32 " edx=%08" PRIx32 "\n",
           f->eax,
           f->ebx,
           f->ecx,
           f->edx);
    printf(" esi=%08" PRIx32 " edi=%08" PRIx32 " esp=%08" PRIx32 " ebp=%08" PRIx32 "\n",
           f->esi,
           f->edi,
           (uint32_t) f->esp,
           f->ebp);
    printf(" cs=%04" PRIx16 " ds=%04" PRIx16 " es=%04" PRIx16 " ss=%04" PRIx16 "\n",
           f->cs,
           f->ds,
           f->es,
           f->ss);
}

/* 返回中断向量的名称（用于调试）。 */
/* 函数：intr_name
   功能：返回给定向量号对应的中断名称字符串。 */
const char *
intr_name(uint8_t vec)
{
    return intr_names[vec];
}
