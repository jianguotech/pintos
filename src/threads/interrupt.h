#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* 中断开关状态。 */
enum intr_level {
    INTR_OFF, /* 中断关闭。 */
    INTR_ON   /* 中断开启。 */
};

/* 函数：intr_get_level
   功能：获取当前中断开关状态
   参数：无
   返回：INTR_OFF - 中断关闭，INTR_ON - 中断开启 */
enum intr_level intr_get_level(void);
/* 函数：intr_set_level
   功能：设置中断开关状态为指定级别
   参数：level - 要设置的中断级别
   返回：设置前的中断级别 */
enum intr_level intr_set_level(enum intr_level);
/* 函数：intr_enable
   功能：开启中断
   参数：无
   返回：设置前的中断级别 */
enum intr_level intr_enable(void);
/* 函数：intr_disable
   功能：关闭中断
   参数：无
   返回：设置前的中断级别 */
enum intr_level intr_disable(void);

/* 中断栈帧结构体（由汇编桩填充）。 */
struct intr_frame {
    /* intr-stubs.S 中 intr_entry 压入的寄存器快照。 */
    uint32_t edi;       /* 保存的 EDI。 */
    uint32_t esi;       /* 保存的 ESI。 */
    uint32_t ebp;       /* 保存的 EBP。 */
    uint32_t esp_dummy; /* 占位，不使用。 */
    uint32_t ebx;       /* 保存的 EBX。 */
    uint32_t edx;       /* 保存的 EDX。 */
    uint32_t ecx;       /* 保存的 ECX。 */
    uint32_t eax;       /* 保存的 EAX。 */
    uint16_t gs, : 16;  /* 保存的 GS 段寄存器。 */
    uint16_t fs, : 16;  /* 保存的 FS 段寄存器。 */
    uint16_t es, : 16;  /* 保存的 ES 段寄存器。 */
    uint16_t ds, : 16;  /* 保存的 DS 段寄存器。 */

    /* intrNN_stub 统一压入的向量号。 */
    uint32_t vec_no; /* 中断向量号。 */

    /* CPU 某些情况下会自动压入错误码，缺失时由 stub 补 0。 */
    uint32_t error_code; /* 错误码。 */

    /* intrNN_stub 压入的帧指针，便于回溯。 */
    void *frame_pointer; /* 保存的 EBP。 */

    /* CPU 自动压入的寄存器状态。 */
    void (*eip)(void); /* 返回地址。 */
    uint16_t cs, : 16; /* eip 对应的代码段选择子。 */
    uint32_t eflags;   /* CPU 标志寄存器。 */
    void *esp;         /* 保存的栈指针。 */
    uint16_t ss, : 16; /* esp 对应的数据段选择子。 */
};

typedef void intr_handler_func(struct intr_frame *);

/* 函数：intr_init
   功能：初始化可编程中断控制器与 IDT 表
   参数：无
   返回：无 */
void intr_init(void);
/* 函数：intr_register_ext
   功能：为外部设备中断注册处理函数
   参数：vec - 中断向量号，handler - 处理函数指针，name - 中断名称
   返回：无 */
void intr_register_ext(uint8_t vec, intr_handler_func *, const char *name);
/* 函数：intr_register_int
   功能：为内部中断或异常注册处理函数，并指定运行权限与中断状态
   参数：vec - 中断向量号，dpl - 权限级别，level - 中断状态，handler - 处理函数指针，name - 中断名称
   返回：无 */
void intr_register_int(uint8_t vec, int dpl, enum intr_level, intr_handler_func *, const char *name);
/* 函数：intr_context
   功能：判断当前是否处于外部中断处理上下文
   参数：无
   返回：true - 处于中断上下文，false - 不处于中断上下文 */
bool intr_context(void);
/* 函数：intr_yield_on_return
   功能：指示中断返回前让调度器切换线程
   参数：无
   返回：无 */
void intr_yield_on_return(void);

/* 函数：intr_dump_frame
   功能：打印中断帧寄存器信息以便调试
   参数：frame - 中断帧指针
   返回：无 */
void intr_dump_frame(const struct intr_frame *);
/* 函数：intr_name
   功能：返回给定向量号对应的调试名称
   参数：vec - 中断向量号
   返回：中断名称字符串 */
const char *intr_name(uint8_t vec);

#endif /* threads/interrupt.h */
