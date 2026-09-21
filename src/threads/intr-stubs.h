#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* 中断桩代码声明。

   汇编文件 intr-stubs.S 为 256 个中断向量生成桩函数，每个桩在完成
   简单的栈处理后跳转到通用入口 intr_entry。这里声明了桩函数指针
   数组 `intr_stubs' 以便 C 代码在初始化 IDT 时使用。 */
typedef void intr_stub_func(void);
extern intr_stub_func *intr_stubs[256];

/* 函数：intr_exit
   功能：中断返回入口
   参数：无
   返回：无 */
void intr_exit(void);

#endif /* threads/intr-stubs.h */
