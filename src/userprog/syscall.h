#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//#include "sys/types.h"
#include "threads/interrupt.h"
#include <stdbool.h>
typedef int pid_t;
/* 文件：syscall.h
   功能：定义系统调用接口相关的函数声明
   描述：本文件定义了用户程序与内核交互的接口
         实现用户态到内核态转换的机制 */

/* 函数：syscall_init
   功能：初始化系统调用处理机制，设置中断门和系统调用处理程序
   参数：无
   返回：无 */
void syscall_init(void);
static void syscall_handler(struct intr_frame *f);

void syscall_halt(void);
void syscall_exit(int status);
pid_t syscall_exec(const char *cmd_line);
int syscall_wait(pid_t tid);
bool syscall_create(const char *file, unsigned initial_size);
bool syscall_remove(const char *file);
int syscall_open(const char *file);
int syscall_filesize(int fd);
int syscall_read(int fd, void *buffer, unsigned size);
int syscall_write(int fd, const void *buffer, unsigned size);
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);

#endif /* userprog/syscall.h */
