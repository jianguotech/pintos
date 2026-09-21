#include "devices/serial.h"
#include "devices/shutdown.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/switch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <console.h>
#include <debug.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 停止操作系统，打印源文件名、行号、
   函数名和用户特定的消息。 */
void debug_panic(const char *file, int line, const char *function, const char *message, ...)
{
    static int level;
    va_list args;

    intr_disable();
    console_panic();

    level++;
    if (level == 1) {
        printf("Kernel PANIC at %s:%d in %s(): ", file, line, function);

        va_start(args, message);
        vprintf(message, args);
        printf("\n");
        va_end(args);

        debug_backtrace();
    }
    else if (level == 2)
        printf("Kernel PANIC recursion at %s:%d in %s().\n",
               file,
               line,
               function);
    else {
        /* 不打印任何东西：这可能是我们递归的原因。 */
    }

    serial_flush();
    shutdown();
    for (;;)
        ;
}

/* 打印线程的调用栈。
   线程可能处于运行、就绪或阻塞状态。 */
static void
print_stacktrace(struct thread *t, void *aux UNUSED)
{
    void *retaddr = NULL, **frame = NULL;
    const char *status = "UNKNOWN";

    switch (t->status) {
    case THREAD_RUNNING:
        status = "RUNNING";
        break;

    case THREAD_READY:
        status = "READY";
        break;

    case THREAD_BLOCKED:
        status = "BLOCKED";
        break;

    default:
        break;
    }

    printf("Call stack of thread `%s' (status %s):", t->name, status);

    if (t == thread_current()) {
        frame = __builtin_frame_address(1);
        retaddr = __builtin_return_address(0);
    }
    else {
        /* 获取基指针和指令指针的值，
           这些值是当该线程调用 switch_threads 时保存的。 */
        struct switch_threads_frame *saved_frame;

        saved_frame = (struct switch_threads_frame *) t->stack;

        /* 跳过已添加到所有线程列表但从未被调度的线程。
           我们可以识别这些线程，因为它们的 `stack' 成员
           要么指向内核栈页的顶部，要么
           switch_threads_frame 的 'eip' 成员指向 switch_entry。
           另见 threads.c。 */
        if (t->stack == (uint8_t *) t + PGSIZE || saved_frame->eip == switch_entry) {
            printf(" thread was never scheduled.\n");
            return;
        }

        frame = (void **) saved_frame->ebp;
        retaddr = (void *) saved_frame->eip;
    }

    printf(" %p", retaddr);
    for (; (uintptr_t) frame >= 0x1000 && frame[0] != NULL; frame = frame[0])
        printf(" %p", frame[1]);
    printf(".\n");
}

/* 打印所有线程的调用栈。 */
void debug_backtrace_all(void)
{
    enum intr_level oldlevel = intr_disable();

    thread_foreach(print_stacktrace, 0);
    intr_set_level(oldlevel);
}
