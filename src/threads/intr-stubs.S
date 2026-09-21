#include "threads/loader.h"

        .text

/* 中断主入口。

   内部或外部中断从某个 intrNN_stub 入口开始，该桩将 `struct intr_frame'
   的 frame_pointer、error_code 与 vec_no 成员压入栈中，然后跳转到此处。

   此处保存剩余的 `struct intr_frame' 成员，建立内核所需的寄存器
   环境，随后调用 intr_handler() 进行实际的中断处理。

   处理结束后通过 fall-through 到 intr_exit 返回中断。 */
.func intr_entry
intr_entry:
	/* 保存中断处理前的寄存器（调用者寄存器）。 */
	pushl %ds
	pushl %es
	pushl %fs
	pushl %gs
	pushal
        
	/* 设置内核运行环境。 */
	cld			/* 设置字符串指令方向为向上（CLD）。 */
	mov $SEL_KDSEG, %eax	/* 初始化段寄存器为内核数据段。 */
	mov %eax, %ds
	mov %eax, %es
	leal 56(%esp), %ebp	/* 设置帧指针（frame pointer）。 */

	/* 调用 C 语言实现的统一中断处理函数 intr_handler。 */
	pushl %esp
.globl intr_handler
	call intr_handler
	addl $4, %esp
.endfunc

/* 中断返回。

   恢复被打断者的寄存器，清除栈上额外的数据，并通过 IRET 返回。

   该函数单独实现是因为在启动用户进程时（见 userprog/process.c
   的 start_process()）会直接调用它。 */
.globl intr_exit
.func intr_exit
intr_exit:
        /* 恢复调用者的寄存器。 */
	popal
	popl %gs
	popl %fs
	popl %es
	popl %ds

        /* 丢弃 `struct intr_frame' 的 vec_no、error_code、frame_pointer 成员。 */
	addl $12, %esp

        /* 返回到调用者。 */
	iret
.endfunc

/* 中断桩代码。

   为 256 个向量生成桩函数 `intr00_stub'..`intrff_stub'，每个桩作
   为对应向量的入口，并将这些函数的地址按顺序存入 `intr_stubs'
   指针数组，便于在 IDT 中引用。

   大多数桩的操作流程为：

     1. 将 %ebp 压入栈（作为 frame_pointer）。
     2. 将 0 压入栈（作为 error_code）。
     3. 将中断号压入栈（vec_no）。

   对于某些中断，CPU 会自动在栈上压入错误码。为了让 %ebp 指向
   错误码位置，这类桩采取不同步骤：

     1. 将 CPU 压入的错误码拷贝一份压入栈中。
     2. 用 %ebp 替换原来的错误码位置。
     3. 压入中断号（vec_no）。 */

	.data
.globl intr_stubs
intr_stubs:

#/* 下面实现了上述步骤 1 和 2（常见情况）：将 %ebp 压入栈并推入 0 作为错误码。 */
#define zero                                    \
	pushl %ebp;                             \
	pushl $0

#/* 对于 CPU 已经在栈上压入错误码的情况，按下列方式保存与调整： */
#define REAL                                    \
        pushl (%esp);                           \
        movl %ebp, 4(%esp)

/* 为中断向量 NUMBER 生成桩代码。
   TYPE 为 `zero'（使用 0 作为错误码）或 `REAL'（CPU 已推入错误码）。 */
#define STUB(NUMBER, TYPE)                      \
	.text;                                  \
.func intr##NUMBER##_stub;			\
intr##NUMBER##_stub:                            \
	TYPE;                                   \
	push $0x##NUMBER;                       \
        jmp intr_entry;                         \
.endfunc;					\
                                                \
	.data;                                  \
	.long intr##NUMBER##_stub;

/* 所有桩代码。 */
STUB(00, zero) STUB(01, zero) STUB(02, zero) STUB(03, zero)
STUB(04, zero) STUB(05, zero) STUB(06, zero) STUB(07, zero)
STUB(08, REAL) STUB(09, zero) STUB(0a, REAL) STUB(0b, REAL)
STUB(0c, zero) STUB(0d, REAL) STUB(0e, REAL) STUB(0f, zero)

STUB(10, zero) STUB(11, REAL) STUB(12, zero) STUB(13, zero)
STUB(14, zero) STUB(15, zero) STUB(16, zero) STUB(17, zero)
STUB(18, REAL) STUB(19, zero) STUB(1a, REAL) STUB(1b, REAL)
STUB(1c, zero) STUB(1d, REAL) STUB(1e, REAL) STUB(1f, zero)

STUB(20, zero) STUB(21, zero) STUB(22, zero) STUB(23, zero)
STUB(24, zero) STUB(25, zero) STUB(26, zero) STUB(27, zero)
STUB(28, zero) STUB(29, zero) STUB(2a, zero) STUB(2b, zero)
STUB(2c, zero) STUB(2d, zero) STUB(2e, zero) STUB(2f, zero)

STUB(30, zero) STUB(31, zero) STUB(32, zero) STUB(33, zero)
STUB(34, zero) STUB(35, zero) STUB(36, zero) STUB(37, zero)
STUB(38, zero) STUB(39, zero) STUB(3a, zero) STUB(3b, zero)
STUB(3c, zero) STUB(3d, zero) STUB(3e, zero) STUB(3f, zero)

STUB(40, zero) STUB(41, zero) STUB(42, zero) STUB(43, zero)
STUB(44, zero) STUB(45, zero) STUB(46, zero) STUB(47, zero)
STUB(48, zero) STUB(49, zero) STUB(4a, zero) STUB(4b, zero)
STUB(4c, zero) STUB(4d, zero) STUB(4e, zero) STUB(4f, zero)

STUB(50, zero) STUB(51, zero) STUB(52, zero) STUB(53, zero)
STUB(54, zero) STUB(55, zero) STUB(56, zero) STUB(57, zero)
STUB(58, zero) STUB(59, zero) STUB(5a, zero) STUB(5b, zero)
STUB(5c, zero) STUB(5d, zero) STUB(5e, zero) STUB(5f, zero)

STUB(60, zero) STUB(61, zero) STUB(62, zero) STUB(63, zero)
STUB(64, zero) STUB(65, zero) STUB(66, zero) STUB(67, zero)
STUB(68, zero) STUB(69, zero) STUB(6a, zero) STUB(6b, zero)
STUB(6c, zero) STUB(6d, zero) STUB(6e, zero) STUB(6f, zero)

STUB(70, zero) STUB(71, zero) STUB(72, zero) STUB(73, zero)
STUB(74, zero) STUB(75, zero) STUB(76, zero) STUB(77, zero)
STUB(78, zero) STUB(79, zero) STUB(7a, zero) STUB(7b, zero)
STUB(7c, zero) STUB(7d, zero) STUB(7e, zero) STUB(7f, zero)

STUB(80, zero) STUB(81, zero) STUB(82, zero) STUB(83, zero)
STUB(84, zero) STUB(85, zero) STUB(86, zero) STUB(87, zero)
STUB(88, zero) STUB(89, zero) STUB(8a, zero) STUB(8b, zero)
STUB(8c, zero) STUB(8d, zero) STUB(8e, zero) STUB(8f, zero)

STUB(90, zero) STUB(91, zero) STUB(92, zero) STUB(93, zero)
STUB(94, zero) STUB(95, zero) STUB(96, zero) STUB(97, zero)
STUB(98, zero) STUB(99, zero) STUB(9a, zero) STUB(9b, zero)
STUB(9c, zero) STUB(9d, zero) STUB(9e, zero) STUB(9f, zero)

STUB(a0, zero) STUB(a1, zero) STUB(a2, zero) STUB(a3, zero)
STUB(a4, zero) STUB(a5, zero) STUB(a6, zero) STUB(a7, zero)
STUB(a8, zero) STUB(a9, zero) STUB(aa, zero) STUB(ab, zero)
STUB(ac, zero) STUB(ad, zero) STUB(ae, zero) STUB(af, zero)

STUB(b0, zero) STUB(b1, zero) STUB(b2, zero) STUB(b3, zero)
STUB(b4, zero) STUB(b5, zero) STUB(b6, zero) STUB(b7, zero)
STUB(b8, zero) STUB(b9, zero) STUB(ba, zero) STUB(bb, zero)
STUB(bc, zero) STUB(bd, zero) STUB(be, zero) STUB(bf, zero)

STUB(c0, zero) STUB(c1, zero) STUB(c2, zero) STUB(c3, zero)
STUB(c4, zero) STUB(c5, zero) STUB(c6, zero) STUB(c7, zero)
STUB(c8, zero) STUB(c9, zero) STUB(ca, zero) STUB(cb, zero)
STUB(cc, zero) STUB(cd, zero) STUB(ce, zero) STUB(cf, zero)

STUB(d0, zero) STUB(d1, zero) STUB(d2, zero) STUB(d3, zero)
STUB(d4, zero) STUB(d5, zero) STUB(d6, zero) STUB(d7, zero)
STUB(d8, zero) STUB(d9, zero) STUB(da, zero) STUB(db, zero)
STUB(dc, zero) STUB(dd, zero) STUB(de, zero) STUB(df, zero)

STUB(e0, zero) STUB(e1, zero) STUB(e2, zero) STUB(e3, zero)
STUB(e4, zero) STUB(e5, zero) STUB(e6, zero) STUB(e7, zero)
STUB(e8, zero) STUB(e9, zero) STUB(ea, zero) STUB(eb, zero)
STUB(ec, zero) STUB(ed, zero) STUB(ee, zero) STUB(ef, zero)

STUB(f0, zero) STUB(f1, zero) STUB(f2, zero) STUB(f3, zero)
STUB(f4, zero) STUB(f5, zero) STUB(f6, zero) STUB(f7, zero)
STUB(f8, zero) STUB(f9, zero) STUB(fa, zero) STUB(fb, zero)
STUB(fc, zero) STUB(fd, zero) STUB(fe, zero) STUB(ff, zero)
