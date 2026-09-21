#include "threads/switch.h"

#### struct thread *switch_threads (struct thread *cur, struct thread *next);
####
#### 从正在执行的线程 CUR 切换到 NEXT，返回值为切换前线程指针。
####
#### 假设 NEXT 线程同样执行 switch_threads()，因而只需保存部分寄存器，
#### 切换栈指针后恢复寄存器即可；同时会把当前栈指针写回 CUR 结构体。\

.globl switch_threads
.func switch_threads
switch_threads:
    # 保存调用者寄存器。
	#
    # 按 SVR4 ABI 约定需保存 %ebx/%ebp/%esi/%edi，
    # 并保证栈帧大小与 thread_create() 构造的一致。
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi

    # 读取 struct thread 中 stack 成员偏移。
.globl thread_stack_ofs
	mov thread_stack_ofs, %edx

    # 将当前栈指针写入 CUR 的 thread->stack。 
	movl SWITCH_CUR(%esp), %eax
	movl %esp, (%eax,%edx,1)

    # 从 NEXT 的 thread->stack 取回栈指针。
	movl SWITCH_NEXT(%esp), %ecx
	movl (%ecx,%edx,1), %esp

    # 恢复寄存器后返回。
	popl %edi
	popl %esi
	popl %ebp
	popl %ebx
        ret
.endfunc

.globl switch_entry
.func switch_entry
switch_entry:
    # 弹出 switch_threads() 的实参。
	addl $8, %esp

    # 调用 thread_schedule_tail(prev)。
	pushl %eax
.globl thread_schedule_tail
	call thread_schedule_tail
	addl $4, %esp

    # 返回后线程正式运行。
	ret
.endfunc
