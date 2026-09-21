#include "devices/timer.h"
#include "devices/pit.h"
#include "lib/kernel/list.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>

/* 有关8254计时器芯片的硬件详细信息，请参阅 [8254]。 */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* 自OS启动以来的计时器滴答数。 */
static int64_t ticks;

/* 每个计时器滴答的循环次数。
   由timer_calibrate()初始化。 */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);
static void real_time_delay(int64_t num, int32_t denom);

/* 设置计时器每秒中断TIMER_FREQ次，
   并注册相应的中断处理程序。 */
void timer_init(void)
{
    pit_configure_channel(0, 2, TIMER_FREQ);                // 计时器100hz跳一次
    intr_register_ext(0x20, timer_interrupt, "8254 Timer"); // 每次触发中断会执行timer_interrupt
}

/* 校准loops_per_tick，用于实现短暂延迟。 */
void timer_calibrate(void)
{
    unsigned high_bit, test_bit;

    ASSERT(intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    /* 将loops_per_tick近似为仍小于一个计时器滴答的最大2的幂。 */
    loops_per_tick = 1u << 10;
    while (!too_many_loops(loops_per_tick << 1)) {
        loops_per_tick <<= 1;
        ASSERT(loops_per_tick != 0);
    }

    /* 调整loops_per_tick的后续8位。 */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops(loops_per_tick | test_bit))
            loops_per_tick |= test_bit;

    printf("%'" PRIu64 " loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* 返回自OS启动以来的计时器滴答数。 */
int64_t
timer_ticks(void)
{
    // 获取之前的中断状态，停止中断
    enum intr_level old_level = intr_disable();
    int64_t t = ticks;         // 每次中断timer_interrupt ticks++
    intr_set_level(old_level); // 恢复之前中断（返回值为上一个中断状态，因为timer_interrupt清除了中断状态）
    return t;
}

/* 返回自THEN以来经过的计时器滴答数，THEN应该是
   timer_ticks()曾经返回的值。 */
int64_t
timer_elapsed(int64_t then)
{
    return timer_ticks() - then;
}

/* 睡眠大约TICKS个计时器滴答。中断必须打开。 */
// void
// timer_sleep (int64_t ticks)
// {
//   int64_t start = timer_ticks (); //清除中断，获取当前计时器中断次数

//   ASSERT (intr_get_level () == INTR_ON);
//   while (timer_elapsed (start) < ticks)  //当开始后计时器增加次数小于目标的次数时
//     thread_yield (); //不断循环切换启动就绪队列的线程，等待计时器中断增加ticks（停-等待）
// }

/* 睡眠大约TICKS个计时器滴答。中断必须打开。 */
void timer_sleep(int64_t ticks)
{
    if (ticks <= 0)
        return;

    ASSERT(intr_get_level() == INTR_ON);

    struct thread *cur = thread_current();
    cur->wakeup_ticks = timer_ticks() + ticks;

    enum intr_level old_level = intr_disable();
    insert_sleeplist(cur);
    thread_block();
    intr_set_level(old_level);
}

/* 睡眠大约MS毫秒。中断必须打开。 */
void timer_msleep(int64_t ms)
{
    real_time_sleep(ms, 1000);
}

/* 睡眠大约US微秒。中断必须打开。 */
void timer_usleep(int64_t us)
{
    real_time_sleep(us, 1000 * 1000);
}

/* 睡眠大约NS纳秒。中断必须打开。 */
void timer_nsleep(int64_t ns)
{
    real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* 忙等待大约MS毫秒。中断不需要打开。

   忙等待浪费CPU周期，如果在计时器滴答之间或更长时间内以
   中断关闭的状态忙等待，将导致计时器滴答丢失。因此，如果
   中断已启用，请改用timer_msleep()。 */
void timer_mdelay(int64_t ms)
{
    real_time_delay(ms, 1000);
}

/* 睡眠大约US微秒。中断不需要打开。

   忙等待浪费CPU周期，如果在计时器滴答之间或更长时间内以
   中断关闭的状态忙等待，将导致计时器滴答丢失。因此，如果
   中断已启用，请改用timer_usleep()。 */
void timer_udelay(int64_t us)
{
    real_time_delay(us, 1000 * 1000);
}

/* 睡眠执行大约NS纳秒。中断不需要打开。

   忙等待浪费CPU周期，如果在计时器滴答之间或更长时间内以
   中断关闭的状态忙等待，将导致计时器滴答丢失。因此，如果
   中断已启用，请改用timer_nsleep()。*/
void timer_ndelay(int64_t ns)
{
    real_time_delay(ns, 1000 * 1000 * 1000);
}

/* 打印计时器统计信息。 */
void timer_print_stats(void)
{
    printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* 计时器中断处理程序。 */
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
    ticks++;
    thread_tick();
    thread_weakeup(ticks); // 唤醒睡眠队列中到达唤醒时间的线程
    if (thread_mlfqs) {
        struct thread *cur = thread_current();
        thread_mlfqs_update_recent_cpu_by_one(cur); // 每次时钟中断更新当前线程的recent_cpu
        if (ticks % TIMER_FREQ == 0) {
            // 每秒更新一次load_avg和recent_cpu
            thread_mlfqs_update_load_avg();
            thread_mlfqs_update_recent_cpu();
        }
        else if (ticks % 4 == 0) {
            thread_mlfqs_update_priority(cur); // 每4时钟中断更新当前线程优先级
        }
    }
}

/* 如果LOOPS次迭代等待超过一个计时器滴答，则返回true，否则返回false。 */
static bool
too_many_loops(unsigned loops)
{
    /* 等待计时器滴答。 */
    int64_t start = ticks;
    while (ticks == start)
        barrier();

    /* 运行LOOPS次循环。 */
    start = ticks;
    busy_wait(loops);

    /* 如果滴答计数改变，我们迭代了太长时间。 */
    barrier();
    return start != ticks;
}

/* 通过循环LOOPS次来实现短暂延迟。

   标记为NO_INLINE是因为代码对齐可能会对时序产生重大影响，
   因此如果该函数在不同位置以不同的方式内联，结果将难以预测。 */
static void NO_INLINE
busy_wait(int64_t loops)
{
    while (loops-- > 0)
        barrier();
}

/* 睡眠大约NUM/DENOM秒。 */
static void
real_time_sleep(int64_t num, int32_t denom)
{
    /* 将NUM/DENOM秒转换为计时器滴答，向下舍入。

          (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM 滴答。
       1 s / TIMER_FREQ 滴答
    */
    int64_t ticks = num * TIMER_FREQ / denom;

    ASSERT(intr_get_level() == INTR_ON);
    if (ticks > 0) {
        /* 我们正在等待至少一个完整的计时器滴答。使用
           timer_sleep()因为它将CPU产量让给其他进程。 */
        timer_sleep(ticks);
    }
    else {
        /* 否则，使用忙等循环以获得更准确的
           子滴答时序。 */
        real_time_delay(num, denom);
    }
}

/* 忙等待大约NUM/DENOM秒。 */
static void
real_time_delay(int64_t num, int32_t denom)
{
    /* 将分子和分母缩小1000倍以避免
       溢出的可能性。 */
    ASSERT(denom % 1000 == 0);
    busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
