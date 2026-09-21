#include "threads/thread.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* 常量：THREAD_MAGIC
   作用：写入 struct thread->magic，用于在运行期间检测栈溢出。
   说明：若线程内核栈越界，该值会被破坏，后续调用 is_thread() 时
         触发断言，有助于尽早发现问题。详见 thread.h 顶部描述。 */
#define THREAD_MAGIC 0xcd6abf4b

/* 全局变量：ready_list
   作用：保存所有处于 THREAD_READY 状态的线程，等待调度器挑选运行。
   注意：对该链表进行插入/删除必须在中断关闭的原子区间内完成。 */
static struct list ready_list;

static struct list sleep_list; // 线程睡眠队列

/* 全局变量：all_list
   作用：记录系统中所有存活线程，首次被调度时加入，退出时移除。
   用途：thread_foreach() 遍历、调试统计与资源管理等。 */
static struct list all_list;

/* 全局变量：idle_thread
   作用：指向空闲线程结构。空闲线程在就绪队列为空时运行，防止 CPU 空转。 */
static struct thread *idle_thread;

/* 全局变量：initial_thread
   作用：指向系统启动阶段创建的首个线程，负责执行 init.c:main()。 */
static struct thread *initial_thread;

/* 全局变量：tid_lock
   作用：保护 allocate_tid() 中静态计数器的互斥锁，防止并发竞争。 */
static struct lock tid_lock;

/* 结构体：kernel_thread_frame
   含义：描述新线程栈顶构造的伪栈帧，使其第一次调度时跳转到 kernel_thread。 */
struct kernel_thread_frame {
    void *eip;             /* 返回地址。 */
    thread_func *function; /* 要调用的函数。 */
    void *aux;             /* 传递给函数的辅助数据。 */
};

/* 统计：记录不同类别线程消耗的时钟节拍数。 */
static long long idle_ticks;   /* 空闲线程累计的 tick 数。 */
static long long kernel_ticks; /* 内核线程累计的 tick 数。 */
static long long user_ticks;   /* 用户态线程累计的 tick 数。 */

/* 常量：TIME_SLICE -> 普通线程最大连续运行的 tick 数。 */
#define TIME_SLICE 4

/* 变量：thread_ticks -> 当前线程自上次让出以来运行的 tick 数。 */
static unsigned thread_ticks;

/* 调度器选择：
   - false（默认）：轮转（round-robin）调度。
   - true：多级反馈队列（MLFQS）调度。
   通过内核命令行选项 "-o mlfqs" 控制。 */
bool thread_mlfqs;
fixed_t load_avg;

/* 函数：kernel_thread
   功能：作为新建内核线程首次运行的入口，负责调用目标函数并在返回时结束线程。 */
static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);

/* 函数：thread_init
   功能：初始化线程调度框架，将当前执行流包装为首个线程。
   注意：调用时需保持关中断状态，返回前不要使用 thread_current()。 */
void thread_init(void)
{
    ASSERT(intr_get_level() == INTR_OFF);

    lock_init(&tid_lock);
    list_init(&ready_list);
    list_init(&all_list);
    list_init(&sleep_list);

    /* 为当前执行流构造线程结构体。 */
    initial_thread = running_thread();
    init_thread(initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid();
}

/* 函数：thread_start
   功能：创建空闲线程并开启抢占式调度。 */
void thread_start(void)
{
    /* 创建空闲线程。 */
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started);
    load_avg = FP_CONST(0);

    /* 启动抢占式调度。 */
    intr_enable();

    /* 等待空闲线程完成初始化。 */
    sema_down(&idle_started);
}

/* 函数：thread_tick
   功能：时钟中断回调，更新统计并在时间片结束时请求抢占。 */
void thread_tick(void)
{
    struct thread *t = thread_current();

    /* 更新统计数据。 */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return();
}

/**
 * 函数名：thread_print_stats
 * 功能  ：打印调度统计信息，包含 idle/kernel/user 三类 tick。
 * 参数  ：无。
 * 返回值：无。 */
void thread_print_stats(void)
{
    printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n", idle_ticks, kernel_ticks, user_ticks);
}

/**
 * 函数名：thread_create
 * 功能  ：创建新的内核线程并加入就绪队列。
 * 参数  ：
 *   - name     ：线程名称（用于调试输出）。
 *   - priority ：初始优先级。
 *   - function ：线程入口函数指针。
 *   - aux      ：传递给入口函数的用户数据。
 * 返回值：成功返回新线程 tid，失败返回 TID_ERROR。
 * 注意事项：
 *   - 在调度器已启动的情况下，新线程可能在本函数返回前运行或退出；如需同步请使用信号量等原语。 */
tid_t thread_create(const char *name, int priority, thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;

    ASSERT(function != NULL);

    /* Allocate thread. */
    t = palloc_get_page(PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread(t, name, priority);
    tid = t->tid = allocate_tid();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame(t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame(t, sizeof *ef);
    ef->eip = (void (*)(void)) kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame(t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    #ifdef USERPROG
    child_status_init(t, tid);
    #endif
    
    /* Add to run queue. */
    thread_unblock(t);
    if (thread_current()->priority < priority)
        thread_yield(); // 创建新线程后，当前线程可能被抢占

    return tid;
}

/* 函数：thread_block
   功能：将当前线程睡眠（状态设为 THREAD_BLOCKED），等待 thread_unblock() 唤醒。
   要求：调用时必须已经关闭中断；通常通过同步原语间接调用，而不是直接使用。 */
void thread_block(void)
{
    ASSERT(!intr_context());
    ASSERT(intr_get_level() == INTR_OFF);

    thread_current()->status = THREAD_BLOCKED;
    schedule();
}

// 比较函数：thread_priority_cmp
// 功能：根据 aux 参数指定的模式比较两个线程的优先级
// 参数：a, b - 要比较的线程元素
//       aux - 比较模式 (enum thread_cmp_mode)
// 返回：根据模式返回比较结果
bool thread_priority_cmp(const struct list_elem *a, const struct list_elem *b, void *aux)
{
    struct thread *t_a = list_entry(a, struct thread, elem);
    struct thread *t_b = list_entry(b, struct thread, elem);
    enum thread_cmp_mode mode = (enum thread_cmp_mode)(intptr_t)aux;

    if (mode == THREAD_CMP_LESS)
        return t_a->priority < t_b->priority;
    else
        return t_a->priority > t_b->priority;
}

/* 函数：thread_unblock
   功能：将阻塞线程 T 转换为就绪状态，并加入 ready_list。
   注意：若 T 未处于阻塞状态则视为错误；函数自身不会抢占当前线程。 */
void thread_unblock(struct thread *t)
{
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    // list_push_back(&ready_list, &t->elem);
    list_insert_ordered(&ready_list, &t->elem, (list_less_func *) &thread_priority_cmp,
                        (void *)(intptr_t)THREAD_CMP_GREATER);
    t->status = THREAD_READY;
    intr_set_level(old_level);
}

/* 函数：thread_name
   功能：获取当前运行线程的名称。 */
const char *thread_name(void)
{
    return thread_current()->name;
}

/* 函数：thread_current
   功能：返回当前运行中的线程，并执行基本一致性检查。 */
struct thread *thread_current(void)
{
    struct thread *t = running_thread();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_RUNNING);

    return t;
}

/* 函数：thread_tid
   功能：获取当前运行线程的 tid（线程标识符）。 */
tid_t thread_tid(void)
{
    return thread_current()->tid;
}

/* 函数：thread_exit
   功能：注销当前线程并切换至调度器选出的下一个线程，不再返回。 */
void thread_exit(void)
{
    ASSERT(!intr_context());

#ifdef USERPROG
    process_exit();
#endif

    /* Remove thread from all threads list, set our status to dying,
       and schedule another process.  That process will destroy us
       when it calls thread_schedule_tail(). */
    intr_disable();
    list_remove(&thread_current()->allelem);
    thread_current()->status = THREAD_DYING;
    schedule();
    NOT_REACHED();
}

/* 函数：thread_yield
   功能：当前线程主动让出 CPU，依据优先度进入就绪队列，等待重新调度。 */
void thread_yield(void)
{
    struct thread *cur = thread_current(); // 获取当前线程
    enum intr_level old_level;

    ASSERT(!intr_context());

    old_level = intr_disable(); // 获取之前中断状态并关闭中断
    if (cur != idle_thread)
        // list_push_back(&ready_list, &cur->elem); // 如果当前线程非空闲则加入就绪队列队尾
        list_insert_ordered(&ready_list, &cur->elem, (list_less_func *) &thread_priority_cmp,
                            (void *)(intptr_t)THREAD_CMP_GREATER); // 按优先级顺序插入就绪队列
    cur->status = THREAD_READY;
    schedule();                // 根据就绪队列调度器选择下一个线程运行
    intr_set_level(old_level); // 恢复之前中断状态
}

/* 函数：thread_foreach
   功能：遍历 all_list，对每个线程执行回调函数 func，并传入 aux。 */
void thread_foreach(thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT(intr_get_level() == INTR_OFF);

    for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, allelem);
        func(t, aux);
    }
}

/* 函数：thread_donate_priority
   功能：递归地向持有锁的线程捐赠优先级
   参数：无（使用当前线程）
   说明：沿着等待链传播优先级，最多传播 MAX_DONATION_DEPTH 层深度 */
void thread_donate_priority(void)
{
    struct thread *t = thread_current();
    int depth = 0;

    /* 沿着等待链递归传播优先级 */
    while (t->waiting_lock != NULL && depth < MAX_DONATION_DEPTH) {
        struct lock *lock = t->waiting_lock;
        struct thread *holder = lock->holder;

        /* 如果锁没有持有者，停止 */
        if (holder == NULL) {
            break;
        }

        /* 如果持有者优先级已经足够高，停止 */
        if (holder->priority >= t->priority) {
            break;
        }

        /* 捐赠：更新持有者的优先级 */
        holder->priority = t->priority;

        /* 如果持有者在 ready_list 中，需要重新排序 */
        if (holder->status == THREAD_READY) {
            list_remove(&holder->elem);
            list_insert_ordered(&ready_list, &holder->elem, thread_priority_cmp,
                                (void *)(intptr_t)THREAD_CMP_GREATER);
        }

        /* 递归：继续检查持有者是否也在等待其他锁 */
        t = holder;
        depth++;
    }
}

/* 函数：thread_update_priority
   功能：根据持有的锁重新计算当前线程的有效优先级
   参数：t - 要更新优先级的线程
   说明：将优先级设置为 pre_priority 和所有持有锁的最高优先级中的最大值 */
void thread_update_priority(struct thread *t)
{
    int max_priority = t->pre_priority;
    struct list_elem *e;

    /* 遍历所有持有的锁，找出等待这些锁的线程中的最高优先级 */
    for (e = list_begin(&t->locks_held); e != list_end(&t->locks_held); e = list_next(e)) {
        struct lock *lock = list_entry(e, struct lock, elem);

        /* 更新锁的 max_priority：找出等待此锁的最高优先级线程 */
        if (!list_empty(&lock->semaphore.waiters)) {
            /* 使用 list_max 查找当前优先级最高的等待线程
               （优先级可能在等待期间因捐赠而改变） */
            struct thread *max_waiter = list_entry(
                list_max(&lock->semaphore.waiters, thread_priority_cmp,
                         (void *)(intptr_t)THREAD_CMP_LESS),
                struct thread,
                elem);
            lock->max_priority = max_waiter->priority;
        }
        else {
            lock->max_priority = PRI_MIN;
        }

        /* 更新最大优先级 */
        if (lock->max_priority > max_priority) {
            max_priority = lock->max_priority;
        }
    }

    /* 设置有效优先级 */
    t->priority = max_priority;
}

/* 函数：thread_set_priority
   功能：设置当前线程的静态优先级并重新依据优先度运行程序 */
void thread_set_priority(int new_priority)
{
    enum intr_level old_level = intr_disable();
    struct thread *cur = thread_current();
    int old_priority = cur->priority;

    cur->pre_priority = new_priority;

    /* 重新计算有效优先级（考虑捐赠） */
    thread_update_priority(cur);

    intr_set_level(old_level);

    /* 如果优先级降低，可能需要让出 CPU */
    if (cur->priority < old_priority) {
        thread_yield();
    }
}

/* 函数：thread_get_priority
   功能：读取当前线程的有效优先级（包含捐赠）。 */
int thread_get_priority(void)
{
    return thread_current()->priority;
}

/* 函数：thread_set_nice
   功能：设置当前线程的 nice 值（供 MLFQS 调度使用）。
   说明：当前尚未实现，调用不会产生效果。 */
void thread_set_nice(int nice UNUSED)
{
    struct thread *cur = thread_current();
    cur->nice = nice;
    thread_mlfqs_update_priority(cur);
    thread_yield();
}

/* 函数：thread_get_nice
   功能：返回当前线程的 nice 值。
   说明：尚未实现，返回固定值 0。 */
int thread_get_nice(void)
{
    return thread_current()->nice;
}

/* 函数：thread_get_load_avg
   功能：返回系统负载平均值乘以 100。
   说明：尚未实现，返回固定值 0。 */
int thread_get_load_avg(void)
{
    return FP_ROUND(FP_MULT_MIX(load_avg, 100));
}

/* 函数：thread_get_recent_cpu
   功能：返回当前线程 recent_cpu 指标乘以 100。
   说明：尚未实现，返回固定值 0。 */
int thread_get_recent_cpu(void)
{
    return FP_ROUND(FP_MULT_MIX(thread_current()->recent_cpu, 100));
}

// 更新系统 load_avg
void thread_mlfqs_update_load_avg()
{
    int ready_threads = list_size(&ready_list);
    if (thread_current() != idle_thread)
        ready_threads++;
    load_avg = FP_ADD(FP_DIV_MIX(FP_MULT(load_avg, FP_CONST(59)), 60),
                      FP_DIV_MIX(FP_CONST(ready_threads), 60));
}
void thread_mlfqs_update_recent_cpu_by_one(struct thread *t)
{
    if (t != idle_thread) {
        t->recent_cpu = FP_ADD_MIX(t->recent_cpu, 1);
    }
}
// 更新所有线程的 recent_cpu
void thread_mlfqs_update_recent_cpu()
{
    for (struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, allelem);
        if (t != idle_thread) {
            fixed_t temp = FP_MULT_MIX(load_avg, 2);
            t->recent_cpu = FP_ADD_MIX(FP_MULT(FP_DIV(temp, FP_ADD_MIX(temp, 1)), t->recent_cpu), t->nice);
            thread_mlfqs_update_priority(t);
        }
    }
}
// 更新线程 t 的优先级
void thread_mlfqs_update_priority(struct thread *t)
{
    if (t == idle_thread)
        return;
    int new_priority = PRI_MAX - FP_INT_PART(FP_DIV_MIX(t->recent_cpu, 4)) - (t->nice * 2);
    if (new_priority > PRI_MAX)
        new_priority = PRI_MAX;
    if (new_priority < PRI_MIN)
        new_priority = PRI_MIN;
    t->priority = new_priority;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
/* 函数：idle
   功能：空闲线程主体，在无可运行线程时休眠等待中断唤醒。 */
static void idle(void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current();
    sema_up(idle_started);

    for (;;) {
        /* Let someone else run. */
        intr_disable();
        thread_block();

        /* 重新开启中断并通过 HLT 节省能耗，sti 与 hlt 连续执行可确保原子性。 */
        asm volatile("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
/* 函数：kernel_thread
   功能：包装普通函数为线程入口，确保先开中断且返回时结束线程。 */
static void kernel_thread(thread_func *function, void *aux)
{
    ASSERT(function != NULL);

    intr_enable(); /* 调度器在关中断状态下运行，需要手动打开。 */
    function(aux); /* 执行线程主体。 */
    thread_exit(); /* 若返回则主动结束线程。 */
}

/* Returns the running thread. */
/* 函数：running_thread
   功能：通过栈指针定位当前正在运行的线程结构体指针。 */
struct thread *running_thread(void)
{
    uint32_t *esp;

    /* 读取当前栈指针并向下取整到页边界，对应 struct thread 起始地址。 */
    asm("mov %%esp, %0" : "=g"(esp));
    return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
/* 函数：is_thread
   功能：判断指针是否指向有效线程结构。 */
static bool is_thread(struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
/* 函数：init_thread
   功能：根据名称和优先级初始化线程结构，默认标记为阻塞。 */
static void init_thread(struct thread *t, const char *name, int priority)
{
    enum intr_level old_level;

    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->pre_priority = priority;
    t->priority = priority;
    t->magic = THREAD_MAGIC;
    t->nice = 0;
    t->recent_cpu = FP_CONST(0);
#ifdef USERPROG
    t->pagedir = NULL;
    list_init(&t->children_list);
    t->c_status = NULL;
    t->parent = NULL;
    t->exec_file = NULL;
    t->ret = -1;
    t->fd_cnt = 2; // 初始化文件描述符计数器，从2开始（0和1分别为stdin和stdout）
    list_init(&t->fd_list);
#endif


    /* 初始化 priority donation 相关字段。 */
    t->waiting_lock = NULL;
    list_init(&t->locks_held);

    old_level = intr_disable();
    list_push_back(&all_list, &t->allelem);
    intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
/* 函数：alloc_frame
   功能：在线程内核栈顶分配 size 字节帧，并返回帧起始地址。 */
static void *alloc_frame(struct thread *t, size_t size)
{
    /* 栈空间按字长对齐分配。 */
    ASSERT(is_thread(t));
    ASSERT(size % sizeof(uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
/* 函数：next_thread_to_run
   功能：从就绪队列取出下一个要运行的线程，若为空返回 idle 线程。 */
static struct thread *next_thread_to_run(void)
{
    if (list_empty(&ready_list))
        return idle_thread; // 就绪队列空则返回空闲线程
    else
        return list_entry(list_pop_front(&ready_list), struct thread, elem); // 否则返回并弹出就绪队列队首线程
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
/* 函数：thread_schedule_tail
   功能：线程切换收尾，激活新线程上下文并清理已终止线程。 */
void thread_schedule_tail(struct thread *prev)
{
    struct thread *cur = running_thread(); // 获取当前线程指针

    ASSERT(intr_get_level() == INTR_OFF);

    /* 将当前线程标记为运行态。 */
    cur->status = THREAD_RUNNING;

    /* 重置时间片计数。 */
    thread_ticks = 0;

#ifdef USERPROG
    /* 激活用户地址空间。 */
    process_activate();
#endif

    /* 若先前线程进入 DYING 状态，则在此释放其线程结构体。 */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) {
        ASSERT(prev != cur);
        palloc_free_page(prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
/* 函数：schedule
   功能：调度器核心逻辑，选择下一个线程并完成上下文切换。 */
static void schedule(void)
{
    struct thread *cur = running_thread();      // 获取当前线程
    struct thread *next = next_thread_to_run(); // 选择下一个要运行的线程
    struct thread *prev = NULL;

    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(cur->status != THREAD_RUNNING);
    ASSERT(is_thread(next));

    if (cur != next)
        prev = switch_threads(cur, next); // 切换线程，返回先前线程指针
    thread_schedule_tail(prev);           // 完成线程切换收尾工作,将 next 标记为运行态，如果 prev 处于 DYING 状态则释放其结构体
}

/* Returns a tid to use for a new thread. */
/* 函数：allocate_tid
   功能：从全局计数器分配唯一线程 tid，受 tid_lock 保护。 */
static tid_t allocate_tid(void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire(&tid_lock);
    tid = next_tid++;
    lock_release(&tid_lock);

    return tid;
}

/* struct thread 中 stack 字段的偏移量，供汇编代码使用。 */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

// 在threads.c中定义休眠线程比较函数
static bool sleeped_thread_cmp(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *t_a = list_entry(a, struct thread, elem);
    struct thread *t_b = list_entry(b, struct thread, elem);

    // 首先按唤醒时间排序
    if (t_a->wakeup_ticks != t_b->wakeup_ticks)
        return t_a->wakeup_ticks < t_b->wakeup_ticks;

    // 如果唤醒时间相同，按优先级从高到低排序（优先级高的排在前面）
    return t_a->priority > t_b->priority;
}

// 将线程加入睡眠队列
void insert_sleeplist(struct thread *t)
{
    ASSERT(intr_get_level() == INTR_OFF);
    list_insert_ordered(&sleep_list, &t->elem, sleeped_thread_cmp, NULL);
}

// 唤醒睡眠队列中到达唤醒时间的线程
void thread_weakeup(int64_t ticks)
{
    while (!list_empty(&sleep_list)) {
        struct thread *t = list_entry(list_front(&sleep_list), struct thread, elem);
        if (t->wakeup_ticks <= ticks) {
            list_pop_front(&sleep_list);
            thread_unblock(t);
        }
        else {
            break;
        }
    }
}

#ifdef USERPROG
void child_status_init(struct thread *t, tid_t tid)
{
    t->parent=thread_current();
    struct child_status *status = malloc(sizeof(struct child_status));
    status->tid = tid;
    sema_init(&status->load_sema, 0);
    sema_init(&status->wait_sema, 0);
    status->exited = false;
    status->waited = false;
    status->ret = -1;
    status->load_success = false;
    list_push_back(&thread_current()->children_list, &status->child_elem);
    t->c_status = status;
}

struct child_status* thread_status_get_by_tid(struct thread *t,tid_t tid)
{
    struct list_elem *e;
    for (e = list_begin(&t->children_list); e != list_end(&t->children_list); e = list_next(e)) {
        struct child_status *status = list_entry(e, struct child_status, child_elem);
        if (status->tid == tid) {
            return status;
        }
    }
    return NULL;
}

struct file_descriptor* thread_fd_get_by_fd(struct thread *t,int fd)
{
    struct list_elem *e;
    for (e = list_begin(&t->fd_list); e != list_end(&t->fd_list); e = list_next(e)) {
        struct file_descriptor *file_desc = list_entry(e, struct file_descriptor, fd_elem);
        if (file_desc->fd == fd) {
            return file_desc;
        }
    }
    return NULL;
}

#endif