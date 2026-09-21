#include "threads/init.h"

#include "devices/input.h"
#include "devices/kbd.h"
#include "devices/rtc.h"
#include "devices/serial.h"
#include "devices/shutdown.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include <console.h>
#include <debug.h>
#include <inttypes.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USERPROG
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#else
#include "tests/threads/tests.h"
#endif
#ifdef FILESYS
#include "devices/block.h"
#include "devices/ide.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* 全局指针：init_page_dir
   含义：指向仅包含内核映射的初始页目录。 */
uint32_t *init_page_dir;

#ifdef FILESYS
/* 配置项：-f -> 启动时是否格式化文件系统设备。 */
static bool format_filesys;

/* 配置项：-filesys / -scratch / -swap -> 覆盖默认块设备名称。 */
static const char *filesys_bdev_name;
static const char *scratch_bdev_name;
#ifdef VM
static const char *swap_bdev_name;
#endif
#endif /* FILESYS */

/* 配置项：-ul -> 用户内存池允许分配的最大页数。 */
static size_t user_page_limit = SIZE_MAX;

static void bss_init(void);
static void paging_init(void);

static char **read_command_line(void);
static char **parse_options(char **argv);
static void run_actions(char **argv);
static void usage(void);

#ifdef FILESYS
static void locate_block_devices(void);
static void locate_block_device(enum block_type, const char *name);
#endif

int main(void) NO_RETURN;

/* Pintos 内核主程序。 */
/* 函数：main
   功能：作为 Pintos 内核入口，依次初始化内核子系统并根据命令行执行动作，最后关机。
   参数：无。
   返回：无；函数标记为 NO_RETURN，执行后不会返回。 */
int main(void)
{
    char **argv;

    /* 清理 BSS 段。 */
    bss_init();

    /* 解析命令行并提取参数选项。 */
    argv = read_command_line();
    argv = parse_options(argv);

    /* 为当前执行流初始化线程结构，并启用控制台锁以便串口输出互斥。 */
    thread_init();
    console_init();

    /* 打印启动信息。 */
    printf("Pintos booting with %'" PRIu32 " kB RAM...\n",
           init_ram_pages * PGSIZE / 1024);

    /* 初始化内存管理。 */
    palloc_init(user_page_limit);
    malloc_init();
    paging_init();

    /* 分段相关初始化。 */
#ifdef USERPROG
    tss_init();
    gdt_init();
#endif

    /* 初始化中断子系统。 */
    intr_init();
    timer_init();
    kbd_init();
    input_init();
#ifdef USERPROG
    exception_init();
    syscall_init();
#endif

    /* 启动调度器并开启中断。 */
    thread_start();
    serial_init_queue();
    timer_calibrate();

#ifdef FILESYS
    /* 初始化文件系统。 */
    ide_init();
    locate_block_devices();
    filesys_init(format_filesys);
#endif

    printf("Boot complete.\n");

    /* 依次执行命令行指定的动作。 */
    run_actions(argv);

    /* 收尾并退出。 */
    shutdown();
    thread_exit();
}

/* 函数：bss_init
   功能：清零内核的 BSS 段，确保所有未显式初始化的静态/全局变量以 0 起始。
   参数：无。
   返回：无。 */
/* 清理 BSS 段：该段应当为零，但加载器不会自动清零，需要手动完成。
   起止地址由链接器符号 _start_bss/_end_bss 给出（见 kernel.lds）。 */
static void
bss_init(void)
{
    extern char _start_bss, _end_bss;
    memset(&_start_bss, 0, &_end_bss - &_start_bss);
}

/* 函数：paging_init
   功能：构造内核初始页目录与页表，为所有物理页建立内核映射并写入 CR3。
   参数：无。
   返回：无。 */
/* 构建内核页目录与页表，完成虚拟地址映射并在 CR3 中启用，返回目录指针。 */
static void
paging_init(void)
{
    uint32_t *pd, *pt;
    size_t page;
    extern char _start, _end_kernel_text;

    pd = init_page_dir = palloc_get_page(PAL_ASSERT | PAL_ZERO);
    pt = NULL;
    for (page = 0; page < init_ram_pages; page++) {
        uintptr_t paddr = page * PGSIZE;
        char *vaddr = ptov(paddr);
        size_t pde_idx = pd_no(vaddr);
        size_t pte_idx = pt_no(vaddr);
        bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

        if (pd[pde_idx] == 0) {
            pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
            pd[pde_idx] = pde_create(pt);
        }

        pt[pte_idx] = pte_create_kernel(vaddr, !in_kernel_text);
    }

    /* 将页目录的物理地址写入 CR3（即 PDBR），以立即启用新建的页表。
       参考 IA32 手册的 MOV 到控制寄存器和页目录基址相关章节。 */
    asm volatile("movl %0, %%cr3" : : "r"(vtop(init_page_dir)));
}

/* 函数：read_command_line
   功能：读取加载器提供的命令行缓冲区，将其拆分为以 NULL 结尾的 argv 数组。
   返回：拆分后的 argv 指针数组。 */
static char **
read_command_line(void)
{
    static char *argv[LOADER_ARGS_LEN / 2 + 1];
    char *p, *end;
    int argc;
    int i;

    argc = *(uint32_t *) ptov(LOADER_ARG_CNT);
    p = ptov(LOADER_ARGS);
    end = p + LOADER_ARGS_LEN;
    for (i = 0; i < argc; i++) {
        if (p >= end)
            PANIC("command line arguments overflow");

        argv[i] = p;
        p += strnlen(p, end - p) + 1;
    }
    argv[argc] = NULL;

    /* 打印解析后的命令行。 */
    printf("Kernel command line:");
    for (i = 0; i < argc; i++)
        if (strchr(argv[i], ' ') == NULL)
            printf(" %s", argv[i]);
        else
            printf(" '%s'", argv[i]);
    printf("\n");

    return argv;
}

/* 函数：parse_options
   功能：遍历解析命令行选项，设置内核配置并返回第一个非选项参数。
   参数：argv —— 当前参数数组指针。
   返回：跳过所有选项后的 argv 指针。 */
/* 解析选项并返回首个非选项参数位置。 */
static char **
parse_options(char **argv)
{
    for (; *argv != NULL && **argv == '-'; argv++) {
        char *save_ptr;
        char *name = strtok_r(*argv, "=", &save_ptr);
        char *value = strtok_r(NULL, "", &save_ptr);

        if (!strcmp(name, "-h"))
            usage();
        else if (!strcmp(name, "-q"))
            shutdown_configure(SHUTDOWN_POWER_OFF);
        else if (!strcmp(name, "-r"))
            shutdown_configure(SHUTDOWN_REBOOT);
#ifdef FILESYS
        else if (!strcmp(name, "-f"))
            format_filesys = true;
        else if (!strcmp(name, "-filesys"))
            filesys_bdev_name = value;
        else if (!strcmp(name, "-scratch"))
            scratch_bdev_name = value;
#ifdef VM
        else if (!strcmp(name, "-swap"))
            swap_bdev_name = value;
#endif
#endif
        else if (!strcmp(name, "-rs"))
            random_init(atoi(value));
        else if (!strcmp(name, "-mlfqs"))
            thread_mlfqs = true;
#ifdef USERPROG
        else if (!strcmp(name, "-ul"))
            user_page_limit = atoi(value);
#endif
        else
            PANIC("unknown option `%s' (use -h for help)", name);
    }

    /* 使用 RTC 时间初始化随机种子；若命令行提供 -rs 则该调用无效。
       在 Bochs 等环境下为避免固定时间种子，可在 pintos 脚本中追加 -r。 */
    random_init(rtc_get_time());

    return argv;
}

/* 函数：run_task
   功能：根据 run 动作执行测试或用户程序，并在完成后回显结果。
   参数：argv —— 指向动作名及其参数的数组。
   返回：无。 */
/* 执行 run 指定的任务。 */
static void
run_task(char **argv)
{
    const char *task = argv[1];

    printf("Executing '%s':\n", task);
#ifdef USERPROG
    process_wait(process_execute(task));
#else
    run_test(task);
#endif
    printf("Execution of '%s' complete.\n", task);
}

/* 函数：run_actions
   功能：按照命令行为顺序匹配并执行动作表中的处理函数，直到遇到 NULL。
   参数：argv —— 动作字符串数组。
   返回：无。 */
/* 执行 ARGV[] 中指定的所有动作，直到遇到以 NULL 结尾的终止标记。 */
static void
run_actions(char **argv)
{
    /* 动作结构定义。 */
    struct action {
        char *name;                    /* 动作名称。 */
        int argc;                      /* 参数个数（含动作名）。 */
        void (*function)(char **argv); /* 处理函数。 */
    };

    /* 支持的动作表。 */
    static const struct action actions[] =
        {
            {"run", 2, run_task},
#ifdef FILESYS
            {"ls", 1, fsutil_ls},
            {"cat", 2, fsutil_cat},
            {"rm", 2, fsutil_rm},
            {"extract", 1, fsutil_extract},
            {"append", 2, fsutil_append},
#endif
            {NULL, 0, NULL},
        };

    while (*argv != NULL) {
        const struct action *a;
        int i;

        /* 定位动作名称。 */
        for (a = actions;; a++)
            if (a->name == NULL)
                PANIC("unknown action `%s' (use -h for help)", *argv);
            else if (!strcmp(*argv, a->name))
                break;

        /* 检查参数是否足够。 */
        for (i = 1; i < a->argc; i++)
            if (argv[i] == NULL)
                PANIC("action `%s' requires %d argument(s)", *argv, a->argc - 1);

        /* 执行动作并前移指针。 */
        a->function(argv);
        argv += a->argc;
    }
}

/* 函数：usage
   功能：输出内核命令行参数及动作说明，并立即关闭电源。
   参数：无。
   返回：无；不会返回调用者。 */
/* 打印命令行帮助信息后关闭电源。 */
static void
usage(void)
{
    printf("\nCommand line syntax: [OPTION...] [ACTION...]\n"
           "Options must precede actions.\n"
           "Actions are executed in the order specified.\n"
           "\nAvailable actions:\n"
#ifdef USERPROG
           "  run 'PROG [ARG...]' 运行指定用户程序并等待结束。\n"
#else
           "  run TEST           执行内核测试用例 TEST。\n"
#endif
#ifdef FILESYS
           "  ls                 列出根目录文件。\n"
           "  cat FILE           将 FILE 输出到控制台。\n"
           "  rm FILE            删除 FILE。\n"
           "以下动作通常通过 pintos 脚本的 -g/-p 间接触发：\n"
           "  extract            将压缩包写入文件系统。\n"
           "  append FILE        向临时设备上的 tar 追加 FILE。\n"
#endif
           "\nOptions:\n"
           "  -h                 显示帮助并关机。\n"
           "  -q                 所有动作完成或崩溃后关机。\n"
           "  -r                 动作完成后重启。\n"
#ifdef FILESYS
           "  -f                 启动时格式化文件系统设备。\n"
           "  -filesys=BDEV      使用指定块设备作为文件系统。\n"
           "  -scratch=BDEV      使用指定块设备作为临时盘。\n"
#ifdef VM
           "  -swap=BDEV         使用指定块设备作为交换分区。\n"
#endif
#endif
           "  -rs=SEED           设定随机数种子。\n"
           "  -mlfqs             启用多级反馈队列调度器。\n"
#ifdef USERPROG
           "  -ul=COUNT          限制用户态可用页数量。\n"
#endif
    );
    shutdown_power_off();
}

#ifdef FILESYS
/* 函数：locate_block_devices
   功能：为 Pintos 各角色确定实际块设备，并应用命令行覆写。
   参数：无。
   返回：无。 */
/* 根据角色定位各类块设备。 */
static void
locate_block_devices(void)
{
    locate_block_device(BLOCK_FILESYS, filesys_bdev_name);
    locate_block_device(BLOCK_SCRATCH, scratch_bdev_name);
#ifdef VM
    locate_block_device(BLOCK_SWAP, swap_bdev_name);
#endif
}

/* 函数：locate_block_device
   功能：根据角色和可选名称选择块设备，若名称存在则精确匹配，否则按枚举顺序取第一个匹配。
   参数：role —— 块设备角色；name —— 覆写名称，可为空。
   返回：无。 */
/* 根据角色与名称决定使用的块设备，若指定 name 则直接匹配，否则按探测顺序选取。 */
static void
locate_block_device(enum block_type role, const char *name)
{
    struct block *block = NULL;

    if (name != NULL) {
        block = block_get_by_name(name);
        if (block == NULL)
            PANIC("No such block device \"%s\"", name);
    }
    else {
        for (block = block_first(); block != NULL; block = block_next(block))
            if (block_type(block) == role)
                break;
    }

    if (block != NULL) {
        printf("%s: using %s\n", block_type_name(role), block_name(block));
        block_set_role(role, block);
    }
}
#endif
