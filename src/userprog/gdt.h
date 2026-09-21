/* 文件：gdt.h
   功能：定义全局描述符表（GDT）相关的常量和函数声明
   描述：GDT是x86保护模式下定义内存段属性和访问权限的数据结构
         本文件定义了段选择子和GDT初始化函数 */

#ifndef USERPROG_GDT_H
#define USERPROG_GDT_H

#include "threads/loader.h"

/* 段选择子（Segment selectors）定义
   功能：定义GDT中各个段的选择子值，用于标识不同的段
   更多选择子由加载器在loader.h中定义 */
#define SEL_UCSEG 0x1B /* 用户代码段选择子（User code selector） */
#define SEL_UDSEG 0x23 /* 用户数据段选择子（User data selector） */
#define SEL_TSS 0x28   /* 任务状态段选择子（Task-state segment） */
#define SEL_CNT 6      /* 段的总数量（Number of segments） */

/* 函数：gdt_init
   功能：初始化全局描述符表（GDT），设置内核和用户段的访问权限
   参数：无
   返回：无 */
void gdt_init(void);

#endif /* userprog/gdt.h */
