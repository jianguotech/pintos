#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

/* 文件：process.h
   功能：定义用户进程管理相关的函数声明
   描述：本文件定义了用户进程的生命周期管理函数
         包括进程的创建、执行、等待、退出和激活等操作 */

#include "threads/thread.h"
#include "threads/synch.h"


/* 函数：process_execute
   功能：执行一个用户程序，创建新进程并加载可执行文件
   参数：file_name - 要执行的可执行文件名
   返回：成功返回新进程的线程ID，失败返回TID_ERROR */
tid_t process_execute(const char *file_name);

/* 函数：process_wait
   功能：等待指定子进程的终止
   参数：tid - 要等待的子进程的线程ID
   返回：子进程的退出状态码，如果进程不存在返回-1 */
int process_wait(tid_t child_tid UNUSED);

/* 函数：process_exit
   功能：终止当前进程，释放相关资源并通知父进程
   参数：无
   返回：无 */
void process_exit(void);

/* 函数：process_activate
   功能：激活当前进程的页目录，设置正确的内存映射
   参数：无
   返回：无 */
void process_activate(void);

#endif /* userprog/process.h */
