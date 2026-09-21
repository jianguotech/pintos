#ifndef THREADS_SWITCH_H
#define THREADS_SWITCH_H

#ifndef __ASSEMBLER__
/* switch_thread() 使用的栈帧布局。 */
struct switch_threads_frame {
    uint32_t edi;        /*  0: 保存的 %edi。 */
    uint32_t esi;        /*  4: 保存的 %esi。 */
    uint32_t ebp;        /*  8: 保存的 %ebp。 */
    uint32_t ebx;        /* 12: 保存的 %ebx。 */
    void (*eip)(void);   /* 16: 返回地址。 */
    struct thread *cur;  /* 20: 参数 CUR。 */
    struct thread *next; /* 24: 参数 NEXT。 */
};

/* 函数：switch_threads
   功能：从当前线程 CUR 切换到线程 NEXT，返回值为切换前线程指针。
   参数：cur - 当前线程指针
         next - 目标线程指针
   返回：切换前的线程指针 */
struct thread *switch_threads(struct thread *cur, struct thread *next);

/* switch_entry() 的栈帧。 */
struct switch_entry_frame {
    void (*eip)(void);
};

/* 函数：switch_entry
   功能：线程切换的入口点
   参数：无
   返回：无 */
void switch_entry(void);

/* 函数：switch_thunk
   功能：用于线程初始化，弹出 CUR/NEXT 参数
   参数：无
   返回：无 */
void switch_thunk(void);
#endif

/* switch.S 中使用的偏移量。 */
#define SWITCH_CUR 20
#define SWITCH_NEXT 24

#endif /* threads/switch.h */
