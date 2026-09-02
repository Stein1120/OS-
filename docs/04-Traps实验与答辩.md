# Lab 4：Traps 实验与答辩记录

## 1. 实验目标

本实验通过阅读 RISC-V 汇编、实现内核栈回溯以及用户级定时 alarm，理解函数调用约定、栈帧结构、用户态与内核态之间的陷阱路径、定时中断和完整寄存器现场恢复。

本实验对应 `traps` 分支，包含：

- 分析 `user/call.asm` 并完成 `answers-traps.txt`。
- 实现 `backtrace()`，沿内核栈帧链打印返回地址。
- 新增 `sigalarm`、`sigreturn` 系统调用，实现周期性用户处理函数。
- 阻止 alarm 处理函数重入，并在完成后恢复被中断代码的全部寄存器。

## 2. 修改文件

| 文件 | 作用 |
| --- | --- |
| `answers-traps.txt` | 回答参数寄存器、内联、返回地址、大小端和可变参数问题。 |
| `kernel/riscv.h` | 新增 `r_fp()`，读取保存于 `s0` 的当前帧指针。 |
| `kernel/printf.c` | 实现 `backtrace()`，并在 `panic()` 时打印内核调用链。 |
| `kernel/defs.h` | 声明 `backtrace()`。 |
| `kernel/proc.h`、`kernel/proc.c` | 保存 alarm 周期、计数器、处理函数地址、重入标志和中断前 trapframe。 |
| `kernel/trap.c` | 在用户态定时中断时计数，到期后保存现场并修改返回 PC。 |
| `kernel/sysproc.c` | 实现 `sys_sigalarm`、`sys_sigreturn`，并在 `sys_sleep` 中调用回溯。 |
| `kernel/syscall.h`、`kernel/syscall.c` | 分配系统调用号并加入内核分发表。 |
| `user/user.h`、`user/usys.pl` | 暴露用户接口并生成汇编系统调用桩。 |
| `Makefile` | 编译 `alarmtest`、`bttest`、`call` 并适配新版工具链。 |

## 3. RISC-V 汇编分析

RISC-V 调用约定使用 `a0` 到 `a7` 传递参数，`a0`、`a1` 也用于返回值，`ra` 保存返回地址，`s0` 通常作为帧指针。

在本次实际生成的 `call.asm` 中：

- `printf("%d %d\n", f(8)+1, 13)` 的格式地址在 `a0`，12 在 `a1`，13 在 `a2`。
- 编译器已内联并计算 `f(8)+1`，`main` 中没有调用 `f`，`f` 中也没有调用 `g`。
- `printf` 位于 `0x64e`。
- `jalr` 位于 `0x3c`，执行后 `ra` 是下一条指令地址 `0x40`。
- 小端示例输出 `He110 World`；大端要把 `i` 改为 `0x726c6400`，数值 57616 不变。
- 格式串要求的参数多于实际参数属于未定义行为，缺失值不是某个固定数字。

## 4. 内核栈回溯

GCC 使用 `s0` 保存当前栈帧的帧指针。在 xv6 的 RISC-V 栈帧中：

```text
fp - 8   ：保存的返回地址 ra
fp - 16  ：调用者的帧指针
```

`backtrace()` 通过 `r_fp()` 取得当前 `fp`，打印 `*(fp-8)`，再把 `fp` 更新为 `*(fp-16)`。每个内核栈占一个 4 KiB 对齐页，因此用 `PGROUNDDOWN`、`PGROUNDUP` 计算边界，防止遍历越过当前内核栈。

`bttest` 调用 `sleep` 后，回溯地址由 `addr2line` 解析为 `sysproc.c → syscall.c → trap.c`，与系统调用路径一致。

## 5. Alarm 实现

### 5.1 状态字段

每个进程保存：

- `alarm_interval`：两次 alarm 之间的 CPU tick 数。
- `alarm_ticks`：本周期已经运行的 tick 数。
- `alarm_handler`：用户态处理函数地址，该地址允许为 0。
- `alarm_active`：处理函数是否正在执行，用于阻止重入。
- `alarm_frame`：alarm 发生前完整的用户寄存器现场。

这些字段在进程表项分配和释放时清零，防止复用表项时继承旧状态。

### 5.2 定时中断触发处理函数

`usertrap()` 通过 `which_dev == 2` 判断定时器中断。若 alarm 已启用且处理函数未运行，则增加计数；到达周期时：

1. 清零计数器。
2. 设置 `alarm_active`。
3. 复制完整 `trapframe` 到 `alarm_frame`。
4. 把 `trapframe->epc` 改为用户处理函数地址。

`usertrapret()` 最终把 `epc` 写入 `sepc`，`sret` 因此不会回到被中断指令，而会从用户处理函数开始执行。

### 5.3 sigreturn 恢复现场

处理函数结束时调用 `sigreturn()`。内核把 `alarm_frame` 完整复制回当前 `trapframe`，恢复原 PC 和所有通用寄存器，然后清除 `alarm_active`。

只恢复 PC 不够：循环变量、指针、栈指针和临时寄存器都可能在处理函数中改变，`test1` 会因此检测到计算结果错误。

`syscall()` 会把系统调用处理函数返回值再次写入 `trapframe->a0`。因此 `sys_sigreturn()` 必须返回中断前保存的 `a0`，否则刚恢复的 `a0` 会被 0 或 -1 覆盖。

### 5.4 阻止重入

慢处理函数运行期间仍可能发生定时中断。只有 `alarm_active == 0` 时才计数和投递新 alarm；`sigreturn` 恢复完成后才重新允许。`test2` 专门验证这一点。

## 6. 测试结果

执行：

```bash
make grade
```

实际结果：

```text
answers-traps.txt: OK
backtrace test: OK
alarmtest: test0: OK
alarmtest: test1: OK
alarmtest: test2: OK
usertests: OK
time: OK
Score: 85/85
```

## 7. 答辩高频问题与参考回答

### Q1：trap、interrupt 和 exception 有什么关系？

trap 是控制流进入内核的总称；interrupt 通常由外部设备异步产生，例如定时器；exception 由当前指令同步产生，例如 `ecall`、非法指令或页故障。

### Q2：`sepc`、`scause`、`stval` 分别是什么？

`sepc` 保存发生陷阱时的程序计数器；`scause` 说明陷阱类型；`stval` 提供与异常相关的附加值，例如发生页故障的虚拟地址。

### Q3：系统调用时为什么 `epc` 要加 4？

`ecall` 是 4 字节指令，陷阱返回时应从其下一条指令继续。若不加 4，返回后会再次执行同一条 `ecall`，形成无限循环。

### Q4：为什么 backtrace 依赖 `-fno-omit-frame-pointer`？

关闭帧指针省略后，编译器持续维护 `s0` 栈帧链，回溯才能按固定偏移找到调用者。若优化掉帧指针，栈布局不再保证存在这条链。

### Q5：如何判断 backtrace 应该停止？

每个 xv6 内核栈只有一个页。初始 `fp` 所在页的上下边界限定了合法范围；下一帧指针越界或到达页顶时停止。

### Q6：alarm 为什么修改 `epc` 就能调用用户函数？

从陷阱返回前，内核会把 `trapframe->epc` 写入硬件 `sepc`，`sret` 将 PC 设置为 `sepc`。所以将 `epc` 改成处理函数地址等价于改变用户态恢复位置。

### Q7：为什么必须保存整个 trapframe？

定时中断可能发生在任意指令，所有用户寄存器都可能包含仍有用的中间状态。处理函数也是普通 C 函数，会覆盖调用者保存型之外的大量寄存器，因此必须完整恢复。

### Q8：为什么 handler 地址为 0 也可能有效？

xv6 用户程序从虚拟地址 0 开始链接，测试程序的第一个函数可能正好位于地址 0。因此不能通过 `handler != 0` 判断 alarm 是否启用，应使用 interval 和独立状态字段。

### Q9：alarm 统计的是墙上时间还是 CPU 时间？

只在该进程从用户态收到定时器中断时增加计数，所以近似统计进程实际消耗的 CPU tick，而不是进程睡眠或未被调度时经过的全部时间。

### Q10：为什么先投递 alarm 再 `yield()` 没问题？

修改后的 trapframe 属于进程自身，调度切换不会丢失。该进程重新获得 CPU 并返回用户态时，仍会使用这个 trapframe，从处理函数地址开始执行。

## 8. 现场演示建议

```bash
make qemu
```

在 xv6 shell 依次运行：

```text
bttest
alarmtest
```

解释回溯的三层地址，并指出 `alarmtest` 的三个测试分别验证单次投递、完整寄存器恢复和防重入。最后运行 `make grade` 展示 `Score: 85/85`。
