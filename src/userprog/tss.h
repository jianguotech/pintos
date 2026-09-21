#ifndef USERPROG_TSS_H
#define USERPROG_TSS_H

/* 文件：tss.h
   功能：定义任务状态段（TSS）管理相关的函数声明
   描述：本文件定义了x86架构特有的任务状态段管理接口
         TSS用于处理用户态到内核态的中断栈切换机制 */

#include <stdint.h>

/* 前向声明：TSS结构体
   TSS包含任务切换时需要保存的所有寄存器状态 */
struct tss;

/* 函数：tss_init
   功能：初始化任务状态段，设置内核栈指针等TSS字段
   参数：无
   返回：无 */
void tss_init(void);

/* 函数：tss_get
   功能：获取当前任务状态段的指针
   参数：无
   返回：指向当前TSS结构体的指针 */
struct tss *tss_get(void);

/* 函数：tss_update
   功能：更新TSS中的内核栈指针，使其指向当前线程的内核栈
   参数：无
   返回：无 */
void tss_update(void);

#endif /* userprog/tss.h */
