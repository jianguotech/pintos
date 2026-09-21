/* 文件：exception.h
   功能：定义异常处理相关的常量和函数声明
   描述：本文件包含了页面错误错误码位定义，以及异常处理系统的函数声明
         这些定义用于处理x86架构中的异常和中断 */

#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* 页面错误（Page fault）错误码位定义
   功能：描述页面错误异常发生的原因，存储在错误码中的各个位
   These error code bits describe the cause of the page fault exception */
#define PF_P 0x1 /* 0: 页不存在（not-present page）；1: 访问权限违例（access rights violation） */
#define PF_W 0x2 /* 0: 读操作（read）；1: 写操作（write） */
#define PF_U 0x4 /* 0: 内核模式（kernel）；1: 用户模式（user process） */

/* 函数：exception_init
   功能：初始化异常处理系统，设置中断描述符表（IDT）中的异常处理程序
   参数：无
   返回：无 */
void exception_init(void);

/* 函数：exception_print_stats
   功能：打印异常处理的统计信息，用于调试和性能分析
   参数：无
   返回：无 */
void exception_print_stats(void);

#endif /* userprog/exception.h */
