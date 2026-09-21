/* 本文件派生自教育操作系统 Nachos 的部分实现，原作者版权声明
   保留在下方（用于合规性说明）。 */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"

#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 初始化信号量 SEMA 为 VALUE。

   信号量是一个非负整数，并支持两个原子操作：

   - down（或 P）：等待信号量值变为正数后将其减一（可能阻塞）。
   - up   （或 V）：将信号量值加一，并唤醒一个等待的线程（若有）。 */
/* 函数：sema_init
   功能：初始化信号量，设置初始计数并清空等待队列。 */
void sema_init(struct semaphore *sema, unsigned value)
{
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/* 信号量的 down(P) 操作：当信号量值为 0 时阻塞，直到可用然后将其减一。
   该操作可能导致睡眠，因此不能在中断上下文中调用。 */
/* 函数：sema_down
   功能：执行 P 操作，必要时阻塞当前线程直到计数可用。 */
void sema_down(struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0) {
        /* 追加到等待队列（不按优先级排序）
           原因：等待线程的优先级可能因捐赠而动态变化，但 waiters 列表不会重新排序，
           因此无法保证列表始终有序。sema_up() 必须用 list_max() 查找当前最高优先级线程。
           插入时排序既无法保证正确性优势，也增加不必要的开销。 */
        list_push_back(&sema->waiters, &thread_current()->elem);
        thread_block();
    }
    sema->value--;
    intr_set_level(old_level);
}

/* 非阻塞的 down 操作：仅当信号量值 > 0 时减一并返回 true，适合中断上下文使用。 */
bool
/* 函数：sema_try_down
   功能：尝试执行 P 操作，若立即成功返回 true，否则 false。 */
sema_try_down(struct semaphore *sema)
{
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0) {
        sema->value--;
        success = true;
    }
    else
        success = false;
    intr_set_level(old_level);

    return success;
}

/* 信号量的 up(V) 操作：增加计数并唤醒一个等待线程（如有）。
   该操作可在中断上下文中调用。 */
void sema_up(struct semaphore *sema)
{
    enum intr_level old_level;
    struct thread *t = NULL;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (!list_empty(&sema->waiters)) {

        struct list_elem *max_elem = list_max(&sema->waiters,
                                              thread_priority_cmp,
                                              (void *)(intptr_t)THREAD_CMP_LESS);
        list_remove(max_elem);
        t = list_entry(max_elem, struct thread, elem);
        thread_unblock(t);
    }
    sema->value++;
    intr_set_level(old_level);

    /* 如果唤醒的线程优先级高于当前线程，让出 CPU */
    if (t != NULL && t->priority > thread_current()->priority) {
        if (intr_context()) {
            intr_yield_on_return();
        } else {
            thread_yield();
        }
    }
}

/* 函数：sema_test_helper
   功能：为自测试模拟两个线程间的信号量交替。 */
static void sema_test_helper(void *sema_);

/* 信号量的自测试：两个线程之间进行“乒乓”式的同步，便于验证实现正确性。 */
/* 函数：sema_self_test
   功能：运行信号量自测试用例。 */
void sema_self_test(void)
{
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++) {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++) {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
    }
}

/* 初始化锁 LOCK。一个锁在任意时刻最多只能被单个线程持有。
   我们的锁不是"递归"的，即当前持有锁的线程再次尝试获取
   同一个锁是错误的。

   锁是初始值为 1 的信号量的特化形式。锁和此类信号量
   的区别有两方面。首先，信号量的值可以大于 1，但锁一次只能
   被单个线程拥有。其次，信号量没有所有者，意味着一个线程可以
   "down"信号量而另一个线程"up"它，但对于锁，同一个线程必须
   既获取又释放它。当这些限制变得繁琐时，这表明应该使用
   信号量而不是锁。 */
/* 函数：lock_init
   功能：初始化互斥锁，包含持有者与内部信号量。 */
void lock_init(struct lock *lock)
{
    ASSERT(lock != NULL);

    lock->holder = NULL;
    lock->max_priority = PRI_MIN;
    sema_init(&lock->semaphore, 1);
}

/* 阻塞式获取锁；可能睡眠，因此禁止在中断上下文调用。 */
/* 函数：lock_acquire
   功能：阻塞式获取锁，成功后记录持有者。 */
void lock_acquire(struct lock *lock)
{
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    enum intr_level old_level = intr_disable();
    struct thread *cur = thread_current();

    /* 如果锁已被持有，进行优先级捐赠 */
    if (lock->holder != NULL) {
        /* 设置当前线程正在等待这个锁 */
        cur->waiting_lock = lock;
        /* 执行递归优先级捐赠 */
        thread_donate_priority();
    }

    intr_set_level(old_level);

    /* 阻塞等待锁 */
    sema_down(&lock->semaphore);//sema_down 是不是一定要在关闭中断运行

    /* 获得锁后的处理 */
    old_level = intr_disable();
    cur->waiting_lock = NULL;  /* 不再等待 */
    lock->holder = cur;
    list_push_back(&cur->locks_held, &lock->elem);  /* 加入持有列表 */
    intr_set_level(old_level);
}

/* 非阻塞地尝试获取锁，适用于中断上下文。 */
bool
/* 函数：lock_try_acquire
   功能：非阻塞尝试获取锁，成功即记录持有者。 */
lock_try_acquire(struct lock *lock)
{
    bool success;

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success)
        lock->holder = thread_current();
    return success;
}

/* 释放锁 LOCK，该锁必须为当前线程所持有。

   中断处理程序无法获取锁，因此在中断处理程序中尝试
   释放锁是没有意义的。 */
/* 函数：lock_release
   功能：释放当前线程持有的锁并唤醒等待者。 */
void lock_release(struct lock *lock)
{
    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    enum intr_level old_level = intr_disable();
    struct thread *cur = thread_current();

    /* 从持有锁列表中移除该锁 */
    list_remove(&lock->elem);

    /* 重新计算当前线程的优先级 */
    thread_update_priority(cur);

    lock->holder = NULL;
    intr_set_level(old_level);

    /* 唤醒等待者（sema_up 会在必要时自动 yield） */
    sema_up(&lock->semaphore);
}

/* 如果当前线程持有锁 LOCK，则返回 true，否则返回 false。
   （注意，测试其他线程是否持有锁将是竞争的。） */
bool
/* 函数：lock_held_by_current_thread
   功能：判断当前线程是否持有指定锁。 */
lock_held_by_current_thread(const struct lock *lock)
{
    ASSERT(lock != NULL);

    return lock->holder == thread_current();
}

/* 列表中的一个信号量。 */
struct semaphore_elem {
    struct list_elem elem;      /* 列表元素。 */
    struct semaphore semaphore; /* 此信号量。 */
};

/* 初始化条件变量 COND。条件变量允许一段代码发信号给
   一个条件，并让协作的代码接收信号并据此行动。 */
/* 函数：cond_init
   功能：初始化条件变量等待队列。 */
void cond_init(struct condition *cond)
{
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
}

/* 释放锁并等待条件信号，采用 Mesa 风格；此过程可能睡眠，禁止在中断中使用。 */
/* 函数：cond_wait
   功能：释放关联锁并等待条件信号，唤醒后重新获取锁。 */
void cond_wait(struct condition *cond, struct lock *lock)
{
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    list_push_back(&cond->waiters, &waiter.elem);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/* 比较两个 semaphore_elem 的优先级（基于等待它们的线程的最高优先级） */
static bool
cond_sema_priority_cmp(const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux UNUSED)
{
    struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

    /* 获取每个信号量等待队列中的最高优先级线程 */
    struct thread *ta = list_entry(list_front(&sa->semaphore.waiters), struct thread, elem);
    struct thread *tb = list_entry(list_front(&sb->semaphore.waiters), struct thread, elem);

    /* 返回true表示a的优先级低于b（用于list_max找到最大值） */
    return ta->priority < tb->priority;
}

/* 如果有任何线程正在等待 COND（由 LOCK 保护），
   则此函数向其中一个发信号以唤醒。
   在调用此函数之前必须持有 LOCK。

   中断处理程序无法获取锁，因此在中断处理程序中尝试
   向条件变量发信号是没有意义的。 */
/* 函数：cond_signal
   功能：唤醒一个等待条件变量的线程（按优先级选择）。 */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    if (!list_empty(&cond->waiters)) {
        /* 找到优先级最高的等待者 */
        struct list_elem *max_elem = list_max(&cond->waiters, cond_sema_priority_cmp, NULL);
        list_remove(max_elem);
        sema_up(&list_entry(max_elem, struct semaphore_elem, elem)->semaphore);
    }
}

/* 唤醒所有线程，如果有的话，正在等待 COND（由 LOCK 保护）。
   在调用此函数之前必须持有 LOCK。

   中断处理程序无法获取锁，因此在中断处理程序中尝试
   向条件变量发信号是没有意义的。 */
/* 函数：cond_broadcast
   功能：唤醒等待条件变量的所有线程。 */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}
