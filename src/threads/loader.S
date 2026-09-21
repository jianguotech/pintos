#include "threads/loader.h"

#### 内核引导加载程序。

#### 此代码应存储在硬盘的第一个扇区中。
#### 当 BIOS 运行时，它将此代码加载到物理地址
#### 0x7c00-0x7e00（512 字节）并跳转到其开头，
#### 在实模式下。引导加载程序将内核加载到内存并跳转
#### 到其入口点，即 start.S 中的 start 函数。
####
#### BIOS 将读取引导加载程序的驱动器号传入 DL，
#### 其中软盘驱动器编号为 0x00, 0x01, ...，硬盘驱动器
#### 编号为 0x80, 0x81, ...。我们希望支持从不同于
#### 引导加载程序的驱动器启动内核，因此我们不利用
#### 这个特性。

	# 运行于实模式（16 位段）。
	.code16

	# 初始化段寄存器与栈（栈自 60 KB 向下生长，内核启动后初始线程继续使用该栈）。

	sub %ax, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov $0xf000, %esp

# 配置串口以便在没有连接 VGA 的情况下报告进度。
# 详情参见 [IntrList]。
	sub %dx, %dx			# 串口 0。
	mov $0xe3, %al			# 9600 bps, N-8-1。
					# AH 已经为 0（初始化端口）。
	int $0x14			# 销毁 AX。

	call puts
	.string "PiLo"

#### 读取每个系统硬盘上的分区表并扫描类型为 0x20 的分区，
#### 这是我们用于 Pintos 内核的类型。
####
#### 阅读我们解析的分区表格式描述，参见 [Partitions]。
####
#### 我们打印状态消息以显示正在扫描的磁盘和分区，
#### 例如在扫描第一个硬盘上的四个分区时显示 hda1234。

	mov $0x80, %dl			# 硬盘 0。
read_mbr:
	sub %ebx, %ebx			# 扇区 0。
	mov $0x2000, %ax		# 使用 0x20000 作为缓冲区。
	mov %ax, %es
	call read_sector
	jc no_such_drive

	# 打印 hd[a-z]。
	call puts
	.string " hd"
	mov %dl, %al
	add $'a' - 0x80, %al
	call putc

	# 检查 MBR 签名——如果不存在，则不是分区的硬盘。
	cmpw $0xaa55, %es:510
	jne next_drive

	mov $446, %si			# 分区表条目 1 的偏移量。
	mov $'1', %al
check_partition:
	# 是未使用的分区吗？
	cmpl $0, %es:(%si)
	je next_partition

	# 打印 [1-4]。
	call putc

	# 是 Pintos 内核分区吗？
	cmpb $0x20, %es:4(%si)
	jne next_partition

	# 是可启动分区吗？
	cmpb $0x80, %es:(%si)
	je load_kernel

next_partition:
	# 此分区不匹配，继续下一个。
	add $16, %si			# 到下一个分区表条目的偏移量。
	inc %al
	cmp $510, %si
	jb check_partition

next_drive:
	# 此驱动器上未找到匹配项，继续下一个。
	inc %dl
	jnc read_mbr

no_such_drive:
no_boot_partition:
	# 在任何地方都未找到 Pintos 内核分区，放弃。
	call puts
	.string "\rNot found\r"

	# 通知 BIOS 启动失败。参见 [IntrList]。
	int $0x18

#### 我们找到了一个内核。内核的驱动器号在 DL 中。内核分区的
#### 分区表条目在 ES:SI 处。我们现在的工作是从磁盘读取内核
#### 并跳转到其起始地址。

load_kernel:
	call puts
	.string "\rLoading"

	# 计算要读取的扇区数。Pintos 内核只是 ELF 格式对象，
	# 没有易于读取的字段来标识其自身大小（参见 [ELF1]）。
	# 但出于其他原因我们将 Pintos 内核限制为 512 kB，
	# 因此只需读取分区的全部内容或从磁盘读取 512 kB，
	# 以较小者为准即可。
	mov %es:12(%si), %ecx		# EBP = 扇区数
	cmp $1024, %ecx			# 将大小限制在 512 kB
	jbe 1f
	mov $1024, %cx
1:

	mov %es:8(%si), %ebx		# EBX = 第一个扇区
	mov $0x2000, %ax		# 起始加载地址：0x20000

next_sector:
	# 将一个扇区读入内存。
	mov %ax, %es			# ES:0000 -> 加载地址
	call read_sector
	jc read_failed

	# 每 16 个扇区 == 8 kB 打印一次 '.' 作为进度指示器。
	test $15, %bl
	jnz 1f
	call puts
	.string "."
1:

	# 前进内存指针和磁盘扇区。
	add $0x20, %ax
	inc %bx
	loop next_sector

	call puts
	.string "\r"

#### 将控制权转移给我们加载的内核。我们从 ELF 头部读取起始地址
#### （参见 [ELF1]），并将其从 32 位线性地址转换为用于实模式的
#### 16:16 段：偏移量地址，然后跳转到转换后的地址。80x86 没有
#### 跳转到保存在寄存器中的绝对段：偏移量的指令，因此实际上
#### 我们将地址存储在临时内存位置，然后通过该位置间接跳转。
#### 为了在引导加载程序中节省 4 字节，我们重用引导加载程序
#### 代码的 4 字节作为此临时指针。

	mov $0x2000, %ax
	mov %ax, %es
	mov %es:0x18, %dx
	mov %dx, start
	movw $0x2000, start + 2
	ljmp *start

read_failed:
start:
	# 磁盘扇区读取失败。
	call puts
1:	.string "\rBad read\r"

	# 通知 BIOS 启动失败。参见 [IntrList]。
	int $0x18

#### 打印字符串子程序。为了在引导加载程序中节省空间，此子程序
#### 从调用后的代码流中获取其以空字符结尾的字符串参数，
#### 然后返回到终止空字符后的字节。此子程序保留所有
#### 通用寄存器。

puts:	xchg %si, %ss:(%esp)
	push %ax
next_char:
	mov %cs:(%si), %al
	inc %si
	test %al, %al
	jz 1f
	call putc
	jmp next_char
1:	pop %ax
	xchg %si, %ss:(%esp)
	ret

#### 字符输出子程序。将 AL 中的字符打印到
#### VGA 显示器和串口 0，使用 BIOS 服务（参见
#### [IntrList]）。保留所有通用寄存器。
####
#### 如果被要求输出回车符，此子程序会自动提供
#### 后续的换行符。

putc:	pusha

1:	sub %bh, %bh			# 页 0。
	mov $0x0e, %ah			# 电传输出服务。
	int $0x10

	mov $0x01, %ah			# 串口输出服务。
	sub %dx, %dx			# 串口 0。
2:	int $0x14			# 销毁 AH。
	test $0x80, %ah			# 输出超时？
	jz 3f
	movw $0x9090, 2b		# 将上面的 "int $0x14" 变为 NOP。

3:
	cmp $'\r', %al
	jne popa_ret
	mov $'\n', %al
	jmp 1b

#### 扇区读取子程序。接受 DL 中的驱动器号（0x80 = 硬盘
#### 0，0x81 = 硬盘 1，...）和 EBX 中的扇区号，并
#### 将指定扇区读取到内存 ES:0000 中。出错时返回进位设置，否则清除。
#### 保留所有通用寄存器。

read_sector:
	pusha
	sub %ax, %ax
	push %ax			# LBA 扇区号 [48:63]
	push %ax			# LBA 扇区号 [32:47]
	push %ebx			# LBA 扇区号 [0:31]
	push %es			# 缓冲区段
	push %ax			# 缓冲区偏移量（始终为 0）
	push $1				# 要读取的扇区数
	push $16			# 数据包大小
	mov $0x42, %ah			# 扩展读取
	mov %sp, %si			# DS:SI -> 数据包
	int $0x13			# 错误代码在 CF 中
	popa				# 弹出 16 字节，保留标志
popa_ret:
	popa
	ret				# 错误代码仍在 CF 中

#### 命令行参数及其计数。
#### 由 `pintos' 实用程序写入并由内核读取。
#### 引导加载程序本身不对命令行执行任何操作。
	.org LOADER_ARG_CNT - LOADER_BASE
	.fill LOADER_ARG_CNT_LEN, 1, 0

	.org LOADER_ARGS - LOADER_BASE
	.fill LOADER_ARGS_LEN, 1, 0

#### 分区表。
	.org LOADER_PARTS - LOADER_BASE
	.fill LOADER_PARTS_LEN, 1, 0

#### 供 BIOS 检查的引导扇区签名。
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
