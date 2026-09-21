#ifndef THREADS_FLAGS_H
#define THREADS_FLAGS_H

/*
   文件：flags.h
   功能：定义 EFLAGS 寄存器相关的常量
   说明：包含 CPU 标志寄存器的位掩码定义
*/

/* EFLAGS 寄存器相关的常量定义 */
#define FLAG_MBS 0x00000002 /* 保留位（必须置位） */
#define FLAG_IF 0x00000200  /* 中断允许位（IF） */

#endif /* threads/flags.h (EFLAGS 标志) */
