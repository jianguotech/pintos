/* 文件：syscall.c
   功能：实现系统调用处理机制
   描述：本文件实现了用户程序与内核交互的接口
         负责处理用户态到内核态的转换，调用系统调用处理程序 */

#include "userprog/syscall.h"

#include <debug.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock filesys_lock;

/* 内部函数声明
   系统调用处理程序 */
static void syscall_handler(struct intr_frame *);

static void syscall_die(void) NO_RETURN;
static void validate_uaddr(const void *uaddr);
static void validate_ubuf(const void *buffer, unsigned size);
static void validate_ustr(const char *str);
static int32_t get_user_i32(const void *uaddr);

/* 函数：syscall_init
   功能：初始化系统调用处理机制，设置中断门和系统调用处理程序
   参数：无
   返回：无 */
void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
    lock_init(&filesys_lock);
}

/* 函数：syscall_handler
   功能：系统调用处理程序，处理所有用户程序发起的系统调用请求
   参数：f - 中断帧指针，包含系统调用时的CPU状态
   返回：无 */
static void
syscall_handler(struct intr_frame *f)
{
      /* 用户态 syscall 约定见 src/lib/user/syscall.c:
          syscall 号与参数都压在用户栈上，返回值放在 eax。 */
      validate_uaddr(f->esp);
      int syscall_num = get_user_i32(f->esp);

      const uint8_t *usp = (const uint8_t *) f->esp;
      switch (syscall_num) {
      case SYS_HALT:
            syscall_halt();
            NOT_REACHED();

      case SYS_EXIT: {
            int status = get_user_i32(usp + 4);
            syscall_exit(status);
            NOT_REACHED();
      }

      case SYS_EXEC: {
            const char *cmd_line = (const char *) (uintptr_t) get_user_i32(usp + 4);
            validate_ustr(cmd_line);
            f->eax = syscall_exec(cmd_line);
            break;
      }

      case SYS_WAIT: {
            pid_t pid = (pid_t) get_user_i32(usp + 4);
            f->eax = syscall_wait(pid);
            break;
      }

      case SYS_CREATE: {
            const char *file = (const char *) (uintptr_t) get_user_i32(usp + 4);
            unsigned initial_size = (unsigned) get_user_i32(usp + 8);
            validate_ustr(file);
            f->eax = syscall_create(file, initial_size);
            break;
      }

      case SYS_REMOVE: {
            const char *file = (const char *) (uintptr_t) get_user_i32(usp + 4);
            validate_ustr(file);
            f->eax = syscall_remove(file);
            break;
      }

      case SYS_OPEN: {
            const char *file = (const char *) (uintptr_t) get_user_i32(usp + 4);
            validate_ustr(file);
            f->eax = syscall_open(file);
            break;
      }

      case SYS_FILESIZE: {
            int fd = get_user_i32(usp + 4);
            f->eax = syscall_filesize(fd);
            break;
      }

      case SYS_READ: {
            int fd = get_user_i32(usp + 4);
            void *buffer = (void *) (uintptr_t) get_user_i32(usp + 8);
            unsigned size = (unsigned) get_user_i32(usp + 12);
            validate_ubuf(buffer, size);
            f->eax = syscall_read(fd, buffer, size);
            break;
      }

      case SYS_WRITE: {
            int fd = get_user_i32(usp + 4);
            const void *buffer = (const void *) (uintptr_t) get_user_i32(usp + 8);
            unsigned size = (unsigned) get_user_i32(usp + 12);
            validate_ubuf(buffer, size);
            f->eax = syscall_write(fd, buffer, size);
            break;
      }

      case SYS_SEEK: {
            int fd = get_user_i32(usp + 4);
            unsigned position = (unsigned) get_user_i32(usp + 8);
            syscall_seek(fd, position);
            break;
      }

      case SYS_TELL: {
            int fd = get_user_i32(usp + 4);
            f->eax = syscall_tell(fd);
            break;
      }

      case SYS_CLOSE: {
            int fd = get_user_i32(usp + 4);
            syscall_close(fd);
            break;
      }

      default:
            syscall_die();
      }
}

static void
syscall_die(void)
{
      syscall_exit(-1);
}

static void
validate_uaddr(const void *uaddr)
{
      if (uaddr == NULL || !is_user_vaddr(uaddr) ||
            pagedir_get_page(thread_current()->pagedir, uaddr) == NULL)
            syscall_die();
}

static void
validate_ubuf(const void *buffer, unsigned size)
{
      if (size == 0)
            return;

      const uint8_t *b = (const uint8_t *) buffer;
      validate_uaddr(b);
      validate_uaddr(b + size - 1);

      /* 覆盖跨页情况：每个页面起始地址检查一次。 */
      uintptr_t start = (uintptr_t) b;
      uintptr_t end = (uintptr_t) b + size - 1;
      for (uintptr_t p = start & ~(uintptr_t) (PGSIZE - 1);
             p <= (end & ~(uintptr_t) (PGSIZE - 1));
             p += PGSIZE)
            validate_uaddr((const void *) p);
}

static void
validate_ustr(const char *str)
{
      validate_uaddr(str);
      for (const char *p = str;; p++) {
            validate_uaddr(p);
            if (*p == '\0')
                  break;
      }
}

static int32_t
get_user_i32(const void *uaddr)
{
      validate_uaddr(uaddr);
      validate_uaddr((const uint8_t *) uaddr + sizeof(int32_t) - 1);
      int32_t value;
      memcpy(&value, uaddr, sizeof value);
      return value;
}

void syscall_halt(void)
{
    shutdown_power_off();
}

void syscall_exit(int status)
{
    struct thread *cur = thread_current();
    cur->ret = status;
    process_exit();
    thread_exit();
}

pid_t syscall_exec(const char *cmd_line)
{
   return process_execute(cmd_line);
}

int syscall_wait(pid_t tid)
{
   return process_wait(tid);
}

bool syscall_create(const char *file, unsigned initial_size)
{
   lock_acquire(&filesys_lock);
   bool flag=filesys_create(file, initial_size);
   lock_release(&filesys_lock);
   return flag;
}

bool syscall_remove(const char *file)
{
   lock_acquire(&filesys_lock);
   bool flag= filesys_remove(file);
   lock_release(&filesys_lock);
   return flag;
}

int syscall_open(const char *file)
{
   lock_acquire(&filesys_lock);
   struct file* f=filesys_open(file);
   if(f==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   struct file_descriptor* file_desc=malloc(sizeof(struct file_descriptor));
   if(file_desc==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   file_desc->fd=thread_current()->fd_cnt++;
   file_desc->file=f;
   list_push_back(&thread_current()->fd_list,&file_desc->fd_elem);
   lock_release(&filesys_lock);
   return file_desc->fd;
}

int syscall_filesize(int fd)
{
   lock_acquire(&filesys_lock);
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   int size=file_length(file_desc->file);
   lock_release(&filesys_lock);
   return size;
}

int syscall_read(int fd, void *buffer, unsigned size)
{
   lock_acquire(&filesys_lock);
   if(fd==0)
   {
       unsigned i;
       for(i=0;i<size;i++)
           ((char *)buffer)[i]=input_getc();
       lock_release(&filesys_lock);
       return size;
   }
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   int read_bytes=file_read(file_desc->file,buffer,size);
   lock_release(&filesys_lock);
   return read_bytes;
}

int syscall_write(int fd, const void *buffer, unsigned size)
{
   lock_acquire(&filesys_lock);
   if(fd==1)
   {
       putbuf(buffer,size);
       lock_release(&filesys_lock);
       return size;
   }
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   int written_bytes=file_write(file_desc->file,buffer,size);
   lock_release(&filesys_lock);
   return written_bytes;
}

void syscall_seek(int fd, unsigned position)
{
   lock_acquire(&filesys_lock);
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc!=NULL)
       file_seek(file_desc->file,position);
   lock_release(&filesys_lock);
}

unsigned syscall_tell(int fd)
{
   lock_acquire(&filesys_lock);
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc==NULL)
   {
       lock_release(&filesys_lock);
       return -1;
   }
   unsigned pos=file_tell(file_desc->file);
   lock_release(&filesys_lock);
   return pos;
}

void syscall_close(int fd)
{
   lock_acquire(&filesys_lock);
   struct file_descriptor* file_desc= thread_fd_get_by_fd(thread_current(),fd);
   if(file_desc!=NULL)
   {
       file_close(file_desc->file);
       list_remove(&file_desc->fd_elem);
       free(file_desc);
   }
   lock_release(&filesys_lock);
}