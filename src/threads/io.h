#ifndef THREADS_IO_H
#define THREADS_IO_H

#include <stddef.h>
#include <stdint.h>

/* 函数：inb
   功能：从指定 I/O 端口读取一个字节
   参数：port - I/O 端口号
   返回：从端口读取的 8 位数据 */
static inline uint8_t
inb(uint16_t port)
{
    /* 参考 IA32 手册中关于 "IN" 指令的描述。 */
    uint8_t data;
    asm volatile("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}

/* 函数：insb
   功能：从 I/O 端口连续读取 cnt 个字节到内存地址 addr
   参数：port - I/O 端口号，addr - 目标内存地址，cnt - 要读取的字节数
   返回：无 */
static inline void
insb(uint16_t port, void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "INS" 指令的描述。 */
    asm volatile("rep insb" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

/* 函数：inw
   功能：从指定 I/O 端口读取一个 16 位（word）值
   参数：port - I/O 端口号
   返回：从端口读取的 16 位数据 */
static inline uint16_t
inw(uint16_t port)
{
    uint16_t data;
    /* 参考 IA32 手册中关于 "IN" 指令的描述。 */
    asm volatile("inw %w1, %w0" : "=a"(data) : "Nd"(port));
    return data;
}

/* 函数：insw
   功能：从 I/O 端口连续读取 cnt 个 16 位值到内存地址 addr
   参数：port - I/O 端口号，addr - 目标内存地址，cnt - 要读取的 16 位字数
   返回：无 */
static inline void
insw(uint16_t port, void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "INS" 指令的描述。 */
    asm volatile("rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

/* 函数：inl
   功能：从指定 I/O 端口读取一个 32 位（long）值
   参数：port - I/O 端口号
   返回：从端口读取的 32 位数据 */
static inline uint32_t
inl(uint16_t port)
{
    /* 参考 IA32 手册中关于 "IN" 指令的描述。 */
    uint32_t data;
    asm volatile("inl %w1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

/* 函数：insl
   功能：从 I/O 端口连续读取 cnt 个 32 位值到内存地址 addr
   参数：port - I/O 端口号，addr - 目标内存地址，cnt - 要读取的 32 位字数
   返回：无 */
static inline void
insl(uint16_t port, void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "INS" 指令的描述。 */
    asm volatile("rep insl" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

/* 函数：outb
   功能：向指定 I/O 端口写入一个字节
   参数：port - I/O 端口号，data - 要写入的 8 位数据
   返回：无 */
static inline void
outb(uint16_t port, uint8_t data)
{
    /* 参考 IA32 手册中关于 "OUT" 指令的描述。 */
    asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

/* 函数：outsb
   功能：将内存缓冲区 addr 中的 cnt 个字节写入 I/O 端口
   参数：port - I/O 端口号，addr - 源内存地址，cnt - 要写入的字节数
   返回：无 */
static inline void
outsb(uint16_t port, const void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "OUTS" 指令的描述。 */
    asm volatile("rep outsb" : "+S"(addr), "+c"(cnt) : "d"(port));
}

/* 函数：outw
   功能：向指定 I/O 端口写入一个 16 位（word）值
   参数：port - I/O 端口号，data - 要写入的 16 位数据
   返回：无 */
static inline void
outw(uint16_t port, uint16_t data)
{
    /* 参考 IA32 手册中关于 "OUT" 指令的描述。 */
    asm volatile("outw %w0, %w1" : : "a"(data), "Nd"(port));
}

/* 函数：outsw
   功能：将内存缓冲区 addr 中的 cnt 个 16 位值写入 I/O 端口
   参数：port - I/O 端口号，addr - 源内存地址，cnt - 要写入的 16 位字数
   返回：无 */
static inline void
outsw(uint16_t port, const void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "OUTS" 指令的描述。 */
    asm volatile("rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port));
}

/* 函数：outl
   功能：向指定 I/O 端口写入一个 32 位（long）值
   参数：port - I/O 端口号，data - 要写入的 32 位数据
   返回：无 */
static inline void
outl(uint16_t port, uint32_t data)
{
    /* 参考 IA32 手册中关于 "OUT" 指令的描述。 */
    asm volatile("outl %0, %w1" : : "a"(data), "Nd"(port));
}

/* 函数：outsl
   功能：将内存缓冲区 addr 中的 cnt 个 32 位值写入 I/O 端口
   参数：port - I/O 端口号，addr - 源内存地址，cnt - 要写入的 32 位字数
   返回：无 */
static inline void
outsl(uint16_t port, const void *addr, size_t cnt)
{
    /* 参考 IA32 手册中关于 "OUTS" 指令的描述。 */
    asm volatile("rep outsl" : "+S"(addr), "+c"(cnt) : "d"(port));
}

#endif /* threads/io.h */
