	#include "threads/loader.h"

#### 内核启动代码。

#### 引导加载器（loader.S）会将内核加载到物理地址
#### 0x20000（128 kB）并跳转到此处定义的 "start"。此处代码
#### 将处理器从实模式切换到 32 位保护模式并调用 main()。

/* 控制寄存器 CR0 中的标志位。 */
#define CR0_PE 0x00000001      /* 保护使能（Protection Enable）。 */
#define CR0_EM 0x00000004      /* 浮点仿真（Emulation）。 */
#define CR0_PG 0x80000000      /* 分页（Paging）。 */
#define CR0_WP 0x00010000      /* 内核模式写保护（Write-Protect）。 */

	.section .start

# 以下代码运行在实模式下，即 16 位代码段中。
	.code16

.func start
.globl start
start:

# 加载器通过 CS = 0x2000, SS = 0x0000, ESP = 0xf000 调用我们，
# 但我们应当初始化其他段寄存器。

	mov $0x2000, %ax
	mov %ax, %ds
	mov %ax, %es

# 设置字符串指令向上增长。
	cld

#### 读取物理内存大小：通过中断 15h 的功能 88h（参见 IntrList），
#### 该调用返回 AX = （物理内存 kB）- 1024。该方法仅支持 <=65MB 的内存，
#### 我们将上限限定为 64MB，以匹配后续页表构建的范围。 

	movb $0x88, %ah
	int $0x15
	addl $1024, %eax	# Total kB memory
	cmp $0x10000, %eax	# Cap at 64 MB
	jbe 1f
	mov $0x10000, %eax
1:	shrl $2, %eax		# Total 4 kB pages
	addr32 movl %eax, init_ram_pages - LOADER_PHYS_BASE - 0x20000

#### 启用 A20。地址线 20 在机器启动时被拉低，
#### 这会阻止访问大约 1 MB 以上的内存。这段代码修复该问题。

# 在忙时轮询状态寄存器。

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# 发送写入输出端口的命令。

	movb $0xd1, %al
	outb %al, $0x64

# 在忙时轮询状态寄存器。

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# 启用 A20 线。

	movb $0xdf, %al
	outb %al, $0x60

# 在忙时轮询状态寄存器。

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

#### 创建临时页目录和页表，并设置页
#### 目录基址寄存器。

# 在 0xf000（60 kB）创建页目录并用零填充。
	mov $0xf00, %ax
	mov %ax, %es
	subl %eax, %eax
	subl %edi, %edi
	movl $0x400, %ecx
	rep stosl

# 添加 PDE 以指向 RAM 前 64 MB 的页表。
# 同时在 LOADER_PHYS_BASE 开始添加相同的 PDE。
# 有关 %eax 中各比特位的描述，请参见 [IA32-v3a] 3.7.6 节
# "页目录和页表条目"。

	movl $0x10007, %eax
	movl $0x11, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	movl %eax, %es:LOADER_PHYS_BASE >> 20(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# 为 RAM 前 64 MB 设置页表，用于建立线性地址到物理地址的一对一映射。
# 有关 %eax 中各比特位的描述，请参见 [IA32-v3a] 3.7.6 节
# "页目录和页表条目"。

	movw $0x1000, %ax
	movw %ax, %es
	movl $0x7, %eax
	movl $0x4000, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# 设置页目录基址寄存器。

	movl $0xf000, %eax
	movl %eax, %cr3

#### 切换到保护模式。

# 首先，禁用中断。在进入 C 代码之前我们不会设置 IDT，
# 因此任何中断都会导致系统崩溃。

	cli

# 保护模式需要一个 GDT，因此将 GDTR 指向我们的 GDT。
# 我们需要 data32 前缀来确保 GDT 描述符的全部 32 位被加载
#（默认情况下只加载 24 位）。
# CPU 不需要 addr32 前缀，但 ELF 不进行 16 位重定位。

	data32 addr32 lgdt gdtdesc - LOADER_PHYS_BASE - 0x20000

# 接着我们在 CR0 中开启以下比特位：
#    PE (Protect Enable)：开启保护模式。
#    PG (Paging)：开启分页。
#    WP (Write Protect)：如果未设置，ring 0 代码将忽略
#       页表中的写保护位（！）。
#    EM (Emulation)：强制浮点指令陷入。
#       我们不支持浮点运算。

	movl %cr0, %eax
	orl $CR0_PE | CR0_PG | CR0_WP | CR0_EM, %eax
	movl %eax, %cr0

# 我们现在处于保护模式的 16 位段中。CPU 仍然在 %cs 的段描述符中
# 缓存了实模式的代码段。我们需要重新加载 %cs，最简单的方法是使用
# 远跳转。因为我们不在 32 位段中运行，所以需要 data32 前缀来跳转到
# 目标段的 32 位偏移量。

	data32 ljmp $SEL_KCSEG, $1f

# 我们现在处于保护模式的 32 位段中。
# 让汇编器知道这一点。

	.code32

# 重新加载所有其他段寄存器和栈指针，使其指向我们新的 GDT。

1:	mov $SEL_KDSEG, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	addl $LOADER_PHYS_BASE, %esp
	movl $0, %ebp			# 空终止 main() 的回溯

#### 调用 main()。

	call main

# main() 不应该返回。如果它返回了，则自旋等待。

1:	jmp 1b
.endfunc

#### GDT

	.align 8
gdt:
	.quad 0x0000000000000000	# 空段。CPU 不使用。
	.quad 0x00cf9a000000ffff	# 系统代码段，基址 0，限长 4 GB。
	.quad 0x00cf92000000ffff        # 系统数据段，基址 0，限长 4 GB。

gdtdesc:
	.word	gdtdesc - gdt - 1	# GDT 的大小，减 1 字节。
	.long	gdt			# GDT 的地址。

#### 物理内存大小，单位为 4 kB 页。此变量导出到内核其余部分。
.globl init_ram_pages
init_ram_pages:
	.long 0
