/* 文件：process.c
   功能：实现用户进程的管理
   描述：本文件实现了用户进程的创建、执行、等待和退出等生命周期管理
         负责加载和执行用户程序，处理进程间的同步 */

#include "userprog/process.h"

#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 内部函数声明
   功能：这些函数用于进程的内部实现 */
static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp, char **argv, int argc);

/* 函数：process_execute
   功能：启动一个新线程运行从FILENAME加载的用户程序
         新线程可能在process_execute()返回之前被调度（甚至退出）
         返回新进程的线程ID，如果无法创建线程则返回TID_ERROR
   参数：file_name - 要执行的可执行文件名
   返回：新线程的ID或TID_ERROR */
tid_t process_execute(const char *file_name)
{
    char *fn_copy;
    char *fn_name;
    tid_t tid;

    /* 创建FILE_NAME的副本
       否则在调用者和load()之间存在竞态条件 */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);

    /* 线程名只需要在 thread_create() 调用期间有效。
       不要在 fn_copy 上分词：start_process() 还需要完整命令行。 */
    fn_name = palloc_get_page(0);
    if (fn_name == NULL) {
        palloc_free_page(fn_copy);
        return TID_ERROR;
    }
    strlcpy(fn_name, file_name, PGSIZE);
    char *save_ptr = NULL;
    char *name = strtok_r(fn_name, " ", &save_ptr);
    if (name == NULL)
        name = fn_name;

    /* 创建一个新线程来执行FILE_NAME */
    tid = thread_create(name, PRI_DEFAULT, start_process, fn_copy);
    palloc_free_page(fn_name);
    if (tid == TID_ERROR)
        palloc_free_page(fn_copy);
    struct child_status *cs=thread_status_get_by_tid(thread_current(),tid);
    if(cs==NULL) return -1;
    sema_down(&cs->load_sema);
    if(!cs->load_success) return -1;
    return tid;
}

/* 函数：start_process
   功能：加载用户进程并开始运行
   描述：这是新进程的入口点，负责初始化进程并跳转到用户代码
   参数：file_name_ - 要执行的文件名（作为线程参数传递）
   返回：无（不会返回） */
static void
start_process(void *file_name_)
{
    char *file_name = file_name_;
    char *argv[128];
    int argc=0;
    struct intr_frame if_;
    bool success;
    char *token, *save_ptr;
    for (token = strtok_r((char *) file_name, " ", &save_ptr); token != NULL;
        token = strtok_r(NULL, " ", &save_ptr)) {
        argv[argc] = token;
        argc++;
    }

    /* 初始化中断帧并加载可执行文件 */
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(argv[0], &if_.eip, &if_.esp, argv, argc);
    thread_current()->c_status->load_success=success;
    sema_up(&thread_current()->c_status->load_sema);

    /* 如果加载失败，退出 */
    palloc_free_page(file_name);
    if (!success)
        thread_exit();

    /* 通过模拟从中断返回来启动用户进程
       由intr_exit实现（在threads/intr-stubs.S中）
       因为intr_exit以`struct intr_frame'的形式在栈上接受所有参数
       我们只需将栈指针（%esp）指向我们的栈帧并跳转到它 */
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
    NOT_REACHED();
}

/* 函数：process_wait
   功能：等待线程TID终止并返回其退出状态
   描述：如果它被内核终止（即由于异常而杀死），返回-1
         如果TID无效，或者如果它不是调用进程的子进程
         或者如果process_wait()已经为给定的TID成功调用
         则立即返回-1，不等待
         此函数将在问题2-2中实现，目前什么都不做
   参数：child_tid - 要等待的子线程ID
   返回：子进程的退出状态，失败返回-1 */
int process_wait(tid_t child_tid UNUSED)
{
    struct child_status *status=thread_status_get_by_tid(thread_current(),child_tid);
    if(status==NULL) return -1;
    if(status->waited) return -1;
    status->waited=true;
    if(!status->exited) sema_down(&status->wait_sema);
    int ret=status->ret;
    list_remove(&status->child_elem);
    free(status);
    return ret;
}

/* 函数：process_exit
   功能：释放当前进程的资源
   描述：在进程退出时调用，负责清理页目录和相关资源
   参数：无
   返回：无 */
void process_exit(void)
{
    struct thread *cur = thread_current();
    uint32_t *pd;

    /* 销毁当前进程的页目录并切换回仅内核页目录 */
    pd = cur->pagedir;
    if (pd != NULL) {
        /* 这里的正确顺序至关重要
           我们必须在切换页目录之前将cur->pagedir设置为NULL
           这样定时器中断就不能切换回进程页目录
           我们必须在销毁进程的页目录之前激活基本页目录
           否则我们的活动页目录将是一个已经被释放（和清除）的 */
        //printf("process_exit: destroying page directory of thread %s (tid %d)\n",
            //    cur->name,
            //    cur->tid);

        printf("%s: exit(%d)\n", cur->name, cur->ret);
        struct child_status *status = cur->c_status;
        status->ret = cur->ret;
        status->exited = true;
        sema_up(&status->wait_sema);
        //关闭当前线程 fd_list 中的所有打开文件
        struct list_elem *e;
        for (e = list_begin(&cur->fd_list); e != list_end(&cur->fd_list); ) {
            struct file_descriptor *file_desc = list_entry(e, struct file_descriptor, fd_elem);
            e = list_next(e);
            file_close(file_desc->file);
            list_remove(&file_desc->fd_elem);
            free(file_desc);
        }
        /* 释放正在执行的可执行文件，撤销写保护 */
        if (cur->exec_file != NULL) {
            file_allow_write(cur->exec_file);
            file_close(cur->exec_file);
            cur->exec_file = NULL;
        }
        cur->pagedir = NULL;
        pagedir_activate(NULL);
        pagedir_destroy(pd);
    }
}

/* 函数：process_activate
   功能：为当前线程中运行用户代码设置CPU
   描述：此函数在每次上下文切换时被调用
         负责激活线程的页表和更新TSS
   参数：无
   返回：无 */
void process_activate(void)
{
    struct thread *t = thread_current();

    /* 激活线程的页表 */
    pagedir_activate(t->pagedir);

    /* 设置线程的内核栈以供处理中断使用 */
    tss_update();
}

/* ELF文件加载部分
   功能：我们加载ELF二进制文件
   描述：以下定义取自ELF规范[ELF1]，几乎逐字照搬 */

/* ELF类型定义，参见[ELF1] 1-2 */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* printf格式化宏定义
   功能：用于在printf()中格式化打印ELF类型 */
#define PE32Wx PRIx32 /* 以十六进制打印Elf32_Word */
#define PE32Ax PRIx32 /* 以十六进制打印Elf32_Addr */
#define PE32Ox PRIx32 /* 以十六进制打印Elf32_Off */
#define PE32Hx PRIx16 /* 以十六进制打印Elf32_Half */

/* ELF可执行文件头结构
   功能：可执行文件头，参见[ELF1] 1-4至1-8
   描述：这出现在ELF二进制文件的最开始，包含文件的基本信息 */
struct Elf32_Ehdr {
    unsigned char e_ident[16]; /* 魔数和其他信息 */
    Elf32_Half e_type;         /* 目标文件类型 */
    Elf32_Half e_machine;      /* 所需的架构 */
    Elf32_Word e_version;      /* 目标文件版本 */
    Elf32_Addr e_entry;        /* 程序入口点的虚拟地址 */
    Elf32_Off e_phoff;         /* 程序头表的文件偏移 */
    Elf32_Off e_shoff;         /* 节头表的文件偏移 */
    Elf32_Word e_flags;        /* 处理器特定标志 */
    Elf32_Half e_ehsize;       /* ELF头的大小（字节） */
    Elf32_Half e_phentsize;    /* 程序头表项的大小 */
    Elf32_Half e_phnum;        /* 程序头表项的数量 */
    Elf32_Half e_shentsize;    /* 节头表项的大小 */
    Elf32_Half e_shnum;        /* 节头表项的数量 */
    Elf32_Half e_shstrndx;     /* 节名称字符串表的节头表索引 */
};

/* ELF程序头结构
   功能：程序头，参见[ELF1] 2-2至2-4
   描述：有e_phnum个，从文件偏移e_phoff开始（参见[ELF1] 1-6）
         描述了如何将文件中的段映射到内存中 */
struct Elf32_Phdr {
    Elf32_Word p_type;   /* 段类型 */
    Elf32_Off p_offset;  /* 段在文件中的偏移 */
    Elf32_Addr p_vaddr;  /* 段在内存中的虚拟地址 */
    Elf32_Addr p_paddr;  /* 段的物理地址（用于物理内存架构） */
    Elf32_Word p_filesz; /* 段在文件中的大小 */
    Elf32_Word p_memsz;  /* 段在内存中的大小 */
    Elf32_Word p_flags;  /* 段标志 */
    Elf32_Word p_align;  /* 段对齐 */
};

/* ELF段类型常量
   功能：p_type的值，参见[ELF1] 2-3 */
#define PT_NULL 0           /* 忽略 */
#define PT_LOAD 1           /* 可加载段 */
#define PT_DYNAMIC 2        /* 动态链接信息 */
#define PT_INTERP 3         /* 动态加载器名称 */
#define PT_NOTE 4           /* 辅助信息 */
#define PT_SHLIB 5          /* 保留 */
#define PT_PHDR 6           /* 程序头表 */
#define PT_STACK 0x6474e551 /* 栈段 */

/* ELF段标志常量
   功能：p_flags的标志，参见[ELF3] 2-3和2-4 */
#define PF_X 1 /* 可执行 */
#define PF_W 2 /* 可写 */
#define PF_R 4 /* 可读 */

/* 内部辅助函数声明
   功能：这些函数用于加载过程中的辅助操作 */
static bool setup_stack(void **esp, int argc, char **argv);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable);

/* 函数：load
   功能：从FILE_NAME加载ELF可执行文件到当前线程
   描述：将可执行文件的入口点存储到*EIP
         并将其初始栈指针存储到*ESP
         成功返回true，否则返回false
   参数：file_name - 要加载的文件名
         eip - 输出参数，存储入口点
         esp - 输出参数，存储栈指针
   返回：成功返回true，失败返回false */
bool load(const char *file_name, void (**eip)(void), void **esp, char **argv, int argc)
{
    struct thread *t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* 分配并激活页目录 */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL)
        goto done;
    process_activate();

    /* 打开可执行文件 */
    file = filesys_open(file_name);
    if (file == NULL) {
        printf("load: %s: open failed\n", file_name);
        goto done;
    }
    file_deny_write(file);
    t->exec_file = file; /* 保存可执行文件句柄以便退出时释放 */

    /* 读取并验证可执行文件头 */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* 读取程序头 */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++) {
        struct Elf32_Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;
        file_ofs += sizeof phdr;
        switch (phdr.p_type) {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
            /* Ignore this segment. */
            break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
            goto done;
        case PT_LOAD:
            if (validate_segment(&phdr, file)) {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint32_t file_page = phdr.p_offset & ~PGMASK;
                uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint32_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0) {
                    /* Normal segment.
                       Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
                }
                else {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *) mem_page, read_bytes, zero_bytes, writable))
                    goto done;
            }
            else
                goto done;
            break;
        }
    }

    /* Set up stack. */
    if (!setup_stack(esp, argc, argv))
        goto done;

    /* Start address. */
    *eip = (void (*)(void)) ehdr.e_entry;

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    if (!success && file != NULL) {
        file_close(file);
        t->exec_file = NULL;
    }
    return success;
}

/* load()的辅助函数 */

/* 内部函数声明 */
static bool install_page(void *upage, void *kpage, bool writable);

/* 函数：validate_segment
   功能：检查PHDR是否描述了FILE中有效的、可加载的段
   描述：返回true表示有效，false表示无效
   参数：phdr - 指向程序头的指针
         file - 要检查的文件
   返回：有效返回true，无效返回false */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;

    /* p_offset must point within FILE. */
    if (phdr->p_offset > (Elf32_Off) file_length(file))
        return false;

    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;

    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;

    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *) phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *) (phdr->p_vaddr + phdr->p_memsz)))
        return false;

    /* 映射不能"绕回"到内核虚拟地址空间 */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;

    /* 禁止映射第0页
       不仅映射第0页是个坏主意，而且如果我们允许
       那么传递空指针给系统调用的用户代码
       可能会通过memcpy()等函数中的空指针断言
       轻易地使内核恐慌 */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* 段是有效的 */
    return true;
}

/* 函数：load_segment
   功能：从FILE的偏移OFS开始加载段到地址UPAGE
   描述：总共初始化READ_BYTES + ZERO_BYTES字节的虚拟内存，如下：
         - UPAGE处的READ_BYTES字节必须从FILE读取，从偏移OFS开始
         - UPAGE + READ_BYTES处的ZERO_BYTES字节必须归零
         此函数初始化的页必须可由用户进程写入（如果WRITABLE为true）
         否则为只读
         成功返回true，如果发生内存分配错误或磁盘读取错误则返回false
   参数：file - 要从中读取的文件
         ofs - 文件中的起始偏移
         upage - 要加载到的用户虚拟地址
         read_bytes - 要从文件读取的字节数
         zero_bytes - 要归零的字节数
         writable - 页是否可写
   返回：成功返回true，失败返回false */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0) {
        /* 计算如何填充此页
           我们将从FILE读取PAGE_READ_BYTES字节
           并将最后的PAGE_ZERO_BYTES字节归零 */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* 获取一页内存 */
        uint8_t *kpage = palloc_get_page(PAL_USER);
        if (kpage == NULL)
            return false;

        /* 加载此页 */
        if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes) {
            palloc_free_page(kpage);
            return false;
        }
        memset(kpage + page_read_bytes, 0, page_zero_bytes);

        /* 将页添加到进程的地址空间 */
        if (!install_page(upage, kpage, writable)) {
            palloc_free_page(kpage);
            return false;
        }

        /* 前进到下一页 */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/* 函数：setup_stack
   功能：通过在用户虚拟内存顶部映射一个零页来创建最小栈
   参数：esp - 输出参数，存储新栈的栈指针地址
   返回：成功返回true，失败返回false */
static bool
setup_stack(void **esp, int argc, char **argv)
{
    uint8_t *kpage;
    bool success = false;

    kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage != NULL) {
        success = install_page(((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
        if (success) {
            *esp = PHYS_BASE - 12;
            for (int i = argc - 1; i >= 0; i--) {
                *esp -= sizeof(char) * strlen(argv[i]) + 1;
                strlcpy(*esp, argv[i], sizeof(char) * strlen(argv[i]) + 1);
                argv[i] = *esp;
            }
            /* 字符串对齐 */
            while ((uintptr_t) (*esp) % 4 != 0) {
                *esp -= 1;
                *((uint8_t *) (*esp)) = 0;
            }
            /* 传递参数地址 */
            *esp -= sizeof(char *);
            *((char **) (*esp)) = NULL;
            for (int i = argc - 1; i >= 0; i--) {
                *esp -= sizeof(char *);
                *((char **)(*esp)) = argv[i];
            }
            /* 传递argv地址 */
            char **argv_addr = *esp;
            *esp -= sizeof(char **);
            *((char ***)(*esp)) = argv_addr;
            /* 传递argc */
            *esp -= sizeof(int);
            *((int *)(*esp)) = argc;
            /* 伪返回地址 */
            *esp -= sizeof(void *);
            *((void **)(*esp)) = NULL;
        }
        else
            palloc_free_page(kpage);
    }
    return success;
}

/* 函数：install_page
   功能：在页表中添加从用户虚拟地址UPAGE到内核虚拟地址KPAGE的映射
   参数：upage - 用户虚拟地址
         kpage - 内核虚拟地址（物理帧）
         writable - 是否可写
   返回：成功返回true，失败返回false */
static bool
install_page(void *upage, void *kpage, bool writable)
{
    struct thread *t = thread_current();

    /* 验证该虚拟地址处还没有页，然后将我们的页映射到那里 */
    return (pagedir_get_page(t->pagedir, upage) == NULL && pagedir_set_page(t->pagedir, upage, kpage, writable));
}
