#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include <console.h>
#include <stdarg.h>
#include <stdio.h>

static void vprintf_helper(char, void *);
static void putchar_have_lock(uint8_t c);

/* 控制台锁。
   VGA 和串口层都有自己的锁定机制，所以可以在任何时候
   安全地调用它们。
   但这个锁对于防止同时发出的 printf() 调用
   混合其输出很有用，否则会显得很混乱。 */
static struct lock console_lock;

/* 普通情况下为真：我们想使用控制台
   锁来避免线程之间的输出混合，如上所述。

   在引导早期（此时锁还不能正常工作
   或控制台锁尚未初始化）或内核发生崩溃后为假。
   在前一种情况下，获取锁会导致
   断言失败，进而导致崩溃，使其变成后一种情况。
   在后一种情况下，如果是由于有问题的
   lock_acquire() 实现导致的崩溃，我们
   可能只是递归。 */
static bool use_console_lock;

/* 如果你给 Pintos 添加足够多的调试输出，
   就有可能尝试从单个线程递归地获取 console_lock。
   下面是一个实际的例子，我向 palloc_free() 添加了一个 printf() 调用。
   这是由此产生的真实回溯：

   lock_console()
   vprintf()
   printf()               - palloc() 尝试再次获取该锁
   palloc_free()
   thread_schedule_tail() - 线程切换时另一个线程死亡
   schedule()
   thread_yield()
   intr_handler()         - 定时器中断
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()            - 一个进程写入控制台
   syscall_handler()
   intr_handler()

   这样的事情非常难以调试，所以我们通过
   使用深度计数器模拟递归锁来避免这个问题。 */
static int console_lock_depth;

/* 写入控制台的字符数。 */
static int64_t write_cnt;

/* 启用控制台锁定。 */
void console_init(void)
{
    lock_init(&console_lock);
    use_console_lock = true;
}

/* 通知控制台内核崩溃正在进行中，
   这将警告它从现在起避免尝试获取控制台锁。 */
void console_panic(void)
{
    use_console_lock = false;
}

/* 打印控制台统计信息。 */
void console_print_stats(void)
{
    printf("Console: %lld characters output\n", write_cnt);
}

/* 获取控制台锁。 */
static void
acquire_console(void)
{
    if (!intr_context() && use_console_lock) {
        if (lock_held_by_current_thread(&console_lock))
            console_lock_depth++;
        else
            lock_acquire(&console_lock);
    }
}

/* 释放控制台锁。 */
static void
release_console(void)
{
    if (!intr_context() && use_console_lock) {
        if (console_lock_depth > 0)
            console_lock_depth--;
        else
            lock_release(&console_lock);
    }
}

/* 如果当前线程持有控制台锁，则返回真，
   否则返回假。 */
static bool
console_locked_by_current_thread(void)
{
    return (intr_context() || !use_console_lock || lock_held_by_current_thread(&console_lock));
}

/* 标准 vprintf() 函数，
   类似于 printf() 但使用 va_list。
   将其输出写入 VGA 显示和串口。 */
int vprintf(const char *format, va_list args)
{
    int char_cnt = 0;

    acquire_console();
    __vprintf(format, args, vprintf_helper, &char_cnt);
    release_console();

    return char_cnt;
}

/* 将字符串 S 写入控制台，后跟一个换行符。 */
int puts(const char *s)
{
    acquire_console();
    while (*s != '\0')
        putchar_have_lock(*s++);
    putchar_have_lock('\n');
    release_console();

    return 0;
}

/* 将缓冲区中的 N 个字符写入控制台。 */
void putbuf(const char *buffer, size_t n)
{
    acquire_console();
    while (n-- > 0)
        putchar_have_lock(*buffer++);
    release_console();
}

/* 将 C 写入 VGA 显示和串口。 */
int putchar(int c)
{
    acquire_console();
    putchar_have_lock(c);
    release_console();

    return c;
}

/* vprintf() 的辅助函数。 */
static void
vprintf_helper(char c, void *char_cnt_)
{
    int *char_cnt = char_cnt_;
    (*char_cnt)++;
    putchar_have_lock(c);
}

/* 将 C 写入 VGA 显示和串口。
   调用者已经在适当时获取了控制台锁。 */
static void
putchar_have_lock(uint8_t c)
{
    ASSERT(console_locked_by_current_thread());
    write_cnt++;
    serial_putc(c);
    vga_putc(c);
}
