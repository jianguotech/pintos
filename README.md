# Pintos

Pintos 操作系统课程项目，包含内核源代码、测试、示例程序和运行工具。

## 代码内容

- `src/threads`：线程管理、优先级调度与优先级捐赠、MLFQS。
- `src/devices`：计时器、中断及设备支持。
- `src/userprog`：用户程序加载、参数传递、进程等待与系统调用。
- `src/filesys`、`src/vm`：文件系统基础代码和虚拟内存项目目录；目录存在不代表扩展项目已完成。
- `src/lib`：内核和用户态公共库。
- `src/tests`：各模块测试与评分脚本。
- `src/examples`：用户态示例程序。
- `src/utils`、`src/misc`：模拟器运行、调试和构建辅助工具。

## 构建与测试

需要 GNU Make、Perl、支持 32 位 x86 的 GCC/Binutils 工具链，以及 QEMU 或 Bochs。非 x86 主机的默认配置使用 `i386-elf-*` 交叉工具链，具体配置见 `src/Make.config`。

在仓库根目录执行：

```sh
make -C src/utils
export PATH="$PWD/src/utils:$PATH"
make -C src/threads
make -C src/threads/build check SIMULATOR=--qemu
make -C src/userprog
make -C src/userprog/build check SIMULATOR=--qemu
```

以上为项目构建入口；本次源码整理未重新运行模拟器测试，不声明测试通过率。

## 来源与许可

基于 Pintos 教学操作系统。保留原始 `LICENSE`、`AUTHORS` 和源码中的版权声明。仓库仅收录项目源码及必要配置，不包含课程报告、演示文稿、压缩备份和编译产物。
