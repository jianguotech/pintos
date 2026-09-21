#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 计数信号量（counting semaphore）。 */
struct semaphore {
    unsigned value;      /* 当前计数值。 */
    struct list waiters; /* 等待线程的链表。 */
};

/* 函数：sema_init
   功能：初始化计数信号量
   参数：sema - 信号量指针，value - 初始计数值
   返回：无 */
void sema_init(struct semaphore *, unsigned value);
/* 函数：sema_down
   功能：执行 P 操作，必要时阻塞线程
   参数：sema - 信号量指针
   返回：无 */
void sema_down(struct semaphore *);
/* 函数：sema_try_down
   功能：尝试执行 P 操作，不阻塞
   参数：sema - 信号量指针
   返回：true - 成功获取，false - 失败 */
bool sema_try_down(struct semaphore *);
/* 函数：sema_up
   功能：执行 V 操作并唤醒等待者
   参数：sema - 信号量指针
   返回：无 */
void sema_up(struct semaphore *);
/* 函数：sema_self_test
   功能：运行信号量自测试
   参数：无
   返回：无 */
void sema_self_test(void);

/* 互斥锁（Lock）。 */
struct lock {
    struct thread *holder;      /* 当前持有锁的线程（用于调试）。 */
    struct semaphore semaphore; /* 内部使用的二进制信号量。 */
    struct list_elem elem;      /* 用于链接到线程的 locks_held 列表。 */
    int max_priority;           /* 等待该锁的线程中的最大优先级。 */
};

/* 函数：lock_init
   功能：初始化互斥锁
   参数：lock - 互斥锁指针
   返回：无 */
void lock_init(struct lock *);
/* 函数：lock_acquire
   功能：获取互斥锁
   参数：lock - 互斥锁指针
   返回：无 */
void lock_acquire(struct lock *);
/* 函数：lock_try_acquire
   功能：尝试获取锁，不阻塞
   参数：lock - 互斥锁指针
   返回：true - 成功获取，false - 失败 */
bool lock_try_acquire(struct lock *);
/* 函数：lock_release
   功能：释放互斥锁
   参数：lock - 互斥锁指针
   返回：无 */
void lock_release(struct lock *);
/* 函数：lock_held_by_current_thread
   功能：判断当前线程是否持有锁
   参数：lock - 互斥锁指针
   返回：true - 当前线程持有，false - 当前线程未持有 */
bool lock_held_by_current_thread(const struct lock *);

/* 条件变量。 */
struct condition {
    struct list waiters; /* 等待的线程列表。 */
};

/* 函数：cond_init
   功能：初始化条件变量
   参数：cond - 条件变量指针
   返回：无 */
void cond_init(struct condition *);
/* 函数：cond_wait
   功能：释放锁并等待条件
   参数：cond - 条件变量指针，lock - 互斥锁指针
   返回：无 */
void cond_wait(struct condition *, struct lock *);
/* 函数：cond_signal
   功能：唤醒一个等待线程
   参数：cond - 条件变量指针，lock - 互斥锁指针
   返回：无 */
void cond_signal(struct condition *, struct lock *);
/* 函数：cond_broadcast
   功能：唤醒所有等待线程
   参数：cond - 条件变量指针，lock - 互斥锁指针
   返回：无 */
void cond_broadcast(struct condition *, struct lock *);

/* 优化屏障。

   编译器不会跨优化屏障重新排序操作。
   有关更多信息，请参阅参考资料指南中的"优化屏障"。*/
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
