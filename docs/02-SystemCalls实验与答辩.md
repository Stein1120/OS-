# Lab 2：System Calls 实验与答辩记录

## 1. 实验目标

通过新增 `trace` 和 `sysinfo` 两个系统调用，掌握 xv6 从用户态到内核态的系统调用路径、系统调用参数传递、进程状态继承、内核数据统计以及安全地向用户地址空间复制数据的方法。

本实验对应 `syscall` 分支，包含：

- `trace(mask)`：按位掩码跟踪当前进程及其后代执行的系统调用，输出 PID、系统调用名和返回值。
- `sysinfo(info)`：向用户态返回当前空闲物理内存字节数和已占用进程表项数。

## 2. 修改文件

| 文件 | 作用 |
| --- | --- |
| `user/user.h` | 声明用户态 `trace`、`sysinfo` 接口，并前置声明 `struct sysinfo`。 |
| `user/usys.pl` | 生成两个系统调用的 RISC-V 汇编桩，由 `a7` 传递系统调用号并执行 `ecall`。 |
| `kernel/syscall.h` | 为 `trace` 和 `sysinfo` 分配系统调用号 22、23。 |
| `kernel/syscall.c` | 将系统调用号映射到内核处理函数，并在调用完成后按掩码输出跟踪结果。 |
| `kernel/sysproc.c` | 实现 `sys_trace` 与 `sys_sysinfo`，解析参数并调用内核辅助函数。 |
| `kernel/proc.h`、`kernel/proc.c` | 保存、初始化和继承跟踪掩码；在持锁状态下统计非 `UNUSED` 进程。 |
| `kernel/kalloc.c` | 在空闲链表锁保护下统计空闲页数，并换算为字节数。 |
| `kernel/defs.h` | 声明内核辅助函数 `freemem` 和 `nproc`。 |
| `Makefile` | 将 `trace`、`sysinfotest` 编译进 `fs.img`，并处理新版工具链兼容性。 |
| `time.txt` | 记录实验投入时间，供官方评分脚本检查。 |

## 3. 系统调用完整路径

以 `trace(mask)` 为例：

1. 用户程序调用 `user/user.h` 中声明的 `trace`。
2. `user/usys.pl` 生成汇编桩，把 `SYS_trace` 放入寄存器 `a7`，参数已按 RISC-V ABI 放在 `a0`，然后执行 `ecall`。
3. CPU 从用户态陷入内核态，陷阱处理代码最终调用 `syscall()`。
4. `syscall()` 从陷阱帧的 `a7` 取得系统调用号，并通过 `syscalls[]` 分发表调用 `sys_trace()`。
5. `sys_trace()` 使用 `argint()` 取得 `a0` 中的掩码，保存到当前进程的 `trace_mask`。
6. 返回值写入陷阱帧的 `a0`，恢复用户态后成为用户函数的返回值。

## 4. 关键设计

### 4.1 trace 掩码

第 `n` 位控制编号为 `n` 的系统调用，因此判断条件为 `trace_mask & (1U << num)`。例如 `read` 的编号是 5，`1 << 5` 等于 32，所以命令 `trace 32 ...` 只跟踪 `read`。

跟踪信息必须在目标系统调用执行后输出，因为此时才能取得真实返回值。`sys_trace` 会在执行过程中立即修改当前进程的掩码，因此当新掩码包含 `trace` 自身时，本次 `trace` 调用也会被输出。

### 4.2 fork 继承跟踪状态

`trace` 的要求是跟踪当前进程及其后代。`fork()` 创建子进程时执行 `np->trace_mask = p->trace_mask`，使子进程继承父进程掩码；`exec()` 只替换用户地址空间而不创建新的 `struct proc`，所以掩码自然继续有效。

新进程表项分配和释放时都把掩码清零，防止旧进程留下的状态污染后续进程。

### 4.3 空闲内存统计

xv6 物理页分配器把所有空闲页组织成 `kmem.freelist` 单链表。`freemem()` 在持有 `kmem.lock` 时遍历链表，每遇到一个节点累加 `PGSIZE`，所以返回单位是字节而不是页数。

锁的作用是阻止其他 CPU 在遍历期间同时执行 `kalloc()` 或 `kfree()` 修改链表，否则可能漏计、重复计数，甚至访问不一致的链表指针。

### 4.4 进程数统计

`nproc()` 遍历固定大小的 `proc[NPROC]`，逐个获取 `p->lock`，统计状态不等于 `UNUSED` 的表项。`USED`、`SLEEPING`、`RUNNABLE`、`RUNNING` 和 `ZOMBIE` 都代表仍被占用的进程表项。

### 4.5 安全返回用户态数据

用户传入的地址不能在内核中直接解引用。`argaddr()` 只从陷阱帧取出地址，本身不保证该地址有效；`sys_sysinfo()` 先在内核栈构造 `struct sysinfo`，再调用 `copyout(p->pagetable, addr, ...)`。`copyout()` 检查并转换用户页表映射，非法地址会返回 `-1`，不会让内核直接访问任意地址。

## 5. 测试过程与结果

在 WSL 的实验目录运行：

```bash
make grade
```

实际测试结果：

```text
trace 32 grep: OK
trace all grep: OK
trace nothing: OK
trace children: OK
sysinfotest: OK
time: OK
Score: 35/35
```

其中测试覆盖：只跟踪指定调用、跟踪全部调用、未启用时无额外输出、子进程继承、错误用户地址、内存分配前后统计变化和 `fork/wait` 前后进程数变化。

测试环境：Ubuntu 26.04 LTS（WSL 2）、GCC 15.2.0、RISC-V GCC 15.2.0、GNU Make 4.4.1、GDB 17.1、QEMU 10.2.1。

## 6. 新版工具链兼容性处理

- 显式使用 `-march=rv64gc -mabi=lp64`，避免新版交叉编译器生成 xv6 2021 启动代码不支持的扩展指令。
- 仅对 `user/sh.o` 的有意递归关闭 GCC 15 的 `infinite-recursion` 错误升级，其他警告仍由 `-Werror` 检查。
- Python 3.14 已移除 `pipes`，评分脚本改用 `shlex.quote`。
- 为旧版 `usertests.c` 的 `rwsbrk` 补齐 `char *` 参数，使函数指针类型符合 GCC 15 的严格检查。

## 7. 答辩高频问题与参考回答

### Q1：系统调用号为什么放在 `a7`，返回值为什么在 `a0`？

这是 xv6 采用的 RISC-V 系统调用约定。普通参数使用 `a0` 到 `a5`，系统调用号放在 `a7`；内核处理完成后把返回值写回陷阱帧的 `a0`。

### Q2：`ecall` 之后发生了什么？

CPU 切换到 supervisor 模式并跳转到陷阱入口，汇编代码保存用户寄存器，内核识别为用户系统调用后推进 `epc`，再调用 `syscall()` 完成分发，最后恢复陷阱帧并返回用户态。

### Q3：为什么用位掩码，而不是保存一个系统调用号？

一个整数的不同位可以同时选择多个系统调用，判断只需一次移位和按位与，空间小且效率高。全选可以通过设置多个低位完成。

### Q4：为什么 `trace 32` 跟踪的是 `read`？

`read` 的系统调用号为 5，第 5 位对应的值是 `1 << 5 = 32`。

### Q5：为什么跟踪输出放在处理函数调用之后？

实验要求打印返回值。调用之前尚不知道成功、失败或实际返回的字节数，只有处理函数结束并把结果写入 `a0` 后才能准确输出。

### Q6：`fork` 和 `exec` 对 `trace_mask` 有什么不同？

`fork` 创建新的 `struct proc`，所以必须显式复制掩码；`exec` 复用当前进程结构，只替换用户内存、页表内容和寄存器现场，因此掩码自动保留。

### Q7：`argaddr()` 是否已经验证用户地址合法？

没有。它只取得参数值。真正的合法性验证由随后访问该地址的 `copyin`、`copyout` 或 `copyinstr` 根据用户页表完成。

### Q8：为什么不能直接写 `*(struct sysinfo *)addr = info`？

`addr` 是当前用户页表中的虚拟地址，内核使用的是另一套地址映射；直接解引用既不安全也不保证映射正确。`copyout` 会逐页查询用户页表并检查权限。

### Q9：为什么统计进程时包括 `ZOMBIE`？

僵尸进程虽然已经退出，但父进程尚未 `wait` 回收，它的进程表项和退出状态仍被占用，因此属于非 `UNUSED` 进程。

### Q10：为什么统计空闲链表和进程状态必须加锁？

xv6 可以在多个 CPU 上运行。若统计时其他 CPU 同时修改链表或进程状态，结果会不一致，遍历链表时还可能读取到正在变化的指针。相应自旋锁保证临界区内数据结构稳定。

## 8. 现场演示建议

启动 xv6：

```bash
make qemu
```

在 xv6 shell 中演示：

```text
trace 32 grep hello README
trace 2147483647 echo hello
sysinfotest
```

第一条说明按位选择 `read`，第二条展示一条命令涉及的多种系统调用，第三条验证内存、进程和非法地址处理。最后退出 QEMU，运行 `make grade` 展示 `Score: 35/35`。
