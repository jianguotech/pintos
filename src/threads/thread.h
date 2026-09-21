#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "threads/fixed-point.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>

struct thread; /* 前向声明，供同步原语使用。 */
struct file;   /* 前向声明，供文件指针使用。 */
#include "threads/synch.h"

/* 线程生命周期状态。 */
enum thread_status {
    THREAD_RUNNING, /* 运行态线程。 */
    THREAD_READY,   /* 就绪但未运行。 */
    THREAD_BLOCKED, /* 等待事件触发。 */
    THREAD_DYING    /* 即将被销毁。 */
};

/* 线程标识符类型。
   您可以将其重新定义为您喜欢的任何类型。 */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* tid_t 的错误值。 */

#ifdef USERPROG

struct child_status {
    tid_t tid;
    int ret;
    bool exited;
    bool waited;
    bool load_success;
    struct semaphore wait_sema;
    struct semaphore load_sema;
    struct list_elem child_elem;
};

struct file_descriptor {
    int fd;                   /* 文件描述符编号。 */
    struct file *file;        /* 指向打开的文件的指针。 */
    struct list_elem fd_elem; /* 链表元素，用于线程的 fd_list。 */
};

#endif

/* 线程优先级。 */
#define PRI_MIN 0      /* 最低优先级。 */
#define PRI_DEFAULT 31 /* 默认优先级。 */
#define PRI_MAX 63     /* 最高优先级。 */

/* 优先级捐赠最大深度。
   限制捐赠链的递归深度，防止死锁或无限循环。
   PintOS 测试要求至少支持 8 层嵌套捐赠。 */
#define MAX_DONATION_DEPTH 8

/* 线程优先级比较模式。
   用于控制 thread_priority_cmp 的比较方向。 */
enum thread_cmp_mode {
    THREAD_CMP_LESS,    /* a < b (用于 list_max 查找最大值，或升序排列) */
    THREAD_CMP_GREATER  /* a > b (用于降序排列，高优先级在前) */
};

/* 一个内核线程或用户进程。

   每个线程结构存储在其自己的 4 kB 页面中。线程结构本身位于
   页面的最底部（偏移量 0）。页面的其余部分为线程的
   内核栈保留，从页面顶部（偏移量 4 kB）向下增长。
   以下是示意图：

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   这有两个要点：

      1. 首先，不允许 `struct thread' 增长得太大。
         如果太大，那么内核栈将没有足够的空间。
         我们的基础 `struct thread' 只有几个字节大小。
         它可能应该保持在 1 kB 以下。

      2. 其次，不允许内核栈增长得过大。
         如果栈溢出，将破坏线程状态。
         因此，内核函数不应将大型结构或数组分配为
         非静态局部变量。请使用
         malloc() 或 palloc_get_page() 进行动态分配。

   这些问题中的任何一个的最初症状可能是
   thread_current() 中的断言失败，该函数检查
   运行线程的 `struct thread' 的 `magic' 成员是否
   设置为 THREAD_MAGIC。栈溢出通常会改变此值，
   触发断言。 */
/* `elem' 成员具有双重用途。它可以是
   运行队列（thread.c）中的元素，或者可以是
   信号量等待列表（synch.c）中的元素。它可以用于这两种方式
   仅仅因为它们是互斥的：只有就绪状态的线程
   在运行队列上，而只有阻塞状态的线程
   在信号量等待列表上。 */
struct thread {
    /* 由 thread.c 拥有。 */
    tid_t tid;                 /* 线程标识符。 */
    enum thread_status status; /* 线程状态。 */
    char name[16];             /* 名称（用于调试目的）。 */
    uint8_t *stack;            /* 保存的栈指针。 */
    int priority;              /* 优先级。 */
    int pre_priority;          /* 基础优先级。 */
    struct list_elem allelem;  /* 所有线程列表的元素。 */
    /* 在 thread.c 和 synch.c 之间共享。 */
    struct list_elem elem; /* 列表元素。可用于 ready_list、sleep_list 或 semaphore 等待列表。 */
    int ret;
#ifdef USERPROG
    /* 由 userprog/process.c 拥有。 */
    uint32_t *pagedir;             /* 页目录。 */
    struct list_elem child_elem;   // 子进程链表元素
    struct list children_list;     // 子进程链表头
    struct child_status *c_status; // 指向当前进程的线程状态结构体指针
    struct thread *parent;         // 指向父进程的线程指针
    struct file *exec_file;        // 当前正在执行的可执行文件
    int fd_cnt;                    /* 文件描述符计数器。 */
    struct list fd_list;           /* 打开文件的列表。 */
#endif

    /* 由 thread.c 拥有。 */
    unsigned magic; /* 检测栈溢出。 */

    /* 睡眠队列专用字段。 */
    int64_t wakeup_ticks; /* 线程应该被唤醒的时间。 */

    /* Priority donation 专用字段。 */
    struct lock *waiting_lock; /* 当前线程正在等待的锁。 */
    struct list locks_held;    /* 该线程持有的所有锁列表。 */

    int nice;           /* 线程的 nice 值。 */
    fixed_t recent_cpu; /* 线程的 recent_cpu 值。 */
};

/* 调度器选择：
   - false（默认）：使用轮转（round-robin）调度。
   - true：使用多级反馈队列调度（MLFQS）。
   通过内核命令行选项 `-o mlfqs` 控制。 */
extern bool thread_mlfqs;

/* 函数：thread_init
   功能：初始化线程子系统，将当前执行流包装为线程
   参数：无
   返回：无 */
void thread_init(void);
/* 函数：thread_start
   功能：创建空闲线程并启动调度器
   参数：无
   返回：无 */
void thread_start(void);

/* 函数：thread_tick
   功能：时钟中断回调，更新统计信息
   参数：无
   返回：无 */
void thread_tick(void);
/* 函数：thread_print_stats
   功能：打印线程调度统计
   参数：无
   返回：无 */
void thread_print_stats(void);

typedef void thread_func(void *);
/* 函数：thread_create
   功能：创建新线程
   参数：name - 线程名称，priority - 优先级，func - 线程函数，aux - 传递给线程函数的参数
   返回：新线程的 tid，失败返回 TID_ERROR */
tid_t thread_create(const char *name, int priority, thread_func *, void *);

/* 函数：thread_block
   功能：阻塞当前线程
   参数：无
   返回：无 */
void thread_block(void);

/* 函数：thread_priority_cmp
   功能：比较两个线程的优先级
   参数：a,b - 要比较的线程的 list_elem
         aux - 比较模式 (enum thread_cmp_mode)
               THREAD_CMP_LESS: 返回 a < b (用于 list_max 或升序)
               THREAD_CMP_GREATER: 返回 a > b (用于降序，高优先级在前)
   返回：根据比较模式返回 true/false */
bool thread_priority_cmp(const struct list_elem *a,
                         const struct list_elem *b,
                         void *aux);

/* 函数：thread_unblock
   功能：唤醒指定线程
   参数：t - 要唤醒的线程指针
   返回：无 */
void thread_unblock(struct thread *);

/* 函数：thread_current
   功能：获取当前线程指针
   参数：无
   返回：当前线程的指针 */
struct thread *thread_current(void);
/* 函数：thread_tid
   功能：获取当前线程 tid
   参数：无
   返回：当前线程的 tid */
tid_t thread_tid(void);
/* 函数：thread_name
   功能：获取当前线程名称
   参数：无
   返回：当前线程的名称字符串 */
const char *thread_name(void);

/* 函数：thread_exit
   功能：终止当前线程
   参数：无
   返回：无（永不返回） */
void thread_exit(void) NO_RETURN;
/* 函数：thread_yield
   功能：当前线程让出 CPU
   参数：无
   返回：无 */
void thread_yield(void);

/* 对线程 t 执行某些操作，给定辅助数据 AUX */
typedef void thread_action_func(struct thread *t, void *aux);
/* 函数：thread_foreach
   功能：对所有线程执行指定操作
   参数：action - 操作函数，aux - 传递给操作函数的参数
   返回：无 */
void thread_foreach(thread_action_func *, void *);

void insert_sleeplist(struct thread *t);

void thread_weakeup(int64_t ticks);

/* Priority donation 相关函数 */
void thread_donate_priority(void);
void thread_update_priority(struct thread *t);

/* 函数：thread_get_priority
   功能：获取当前线程优先级
   参数：无
   返回：当前线程的优先级 */
int thread_get_priority(void);
/* 函数：thread_set_priority
   功能：设置当前线程优先级
   参数：priority - 新的优先级
   返回：无 */
void thread_set_priority(int);

/* 函数：thread_get_nice
   功能：获取 nice 值
   参数：无
   返回：当前线程的 nice 值 */
int thread_get_nice(void);
/* 函数：thread_set_nice
   功能：设置 nice 值
   参数：nice - 新的 nice 值
   返回：无 */
void thread_set_nice(int);
/* 函数：thread_get_recent_cpu
   功能：获取 recent_cpu 值
   参数：无
   返回：当前线程的 recent_cpu 值 */
int thread_get_recent_cpu(void);
/* 函数：thread_get_load_avg
   功能：获取负载平均值
   参数：无
   返回：系统负载平均值 */
int thread_get_load_avg(void);
// MLFQS 相关函数
// 更新系统 load_avg
void thread_mlfqs_update_load_avg();
// 更新所有线程的 recent_cpu
void thread_mlfqs_update_recent_cpu();
// 更新线程 t 的优先级
void thread_mlfqs_update_priority(struct thread *t);
// 时钟中断时将线程 t 的 recent_cpu 加一
void thread_mlfqs_update_recent_cpu_by_one(struct thread *t);

#ifdef USERPROG

void child_status_init(struct thread *t, tid_t tid);

struct child_status *thread_status_get_by_tid(struct thread *t, tid_t tid);

struct file_descriptor *thread_fd_get_by_fd(struct thread *t, int fd);

#endif

#endif /* threads/thread.h */
