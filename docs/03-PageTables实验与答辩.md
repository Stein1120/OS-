# Lab 3：Page Tables 实验与答辩记录

## 1. 实验目标

通过共享只读页、递归打印页表和检测页访问状态，理解 RISC-V Sv39 三级页表结构、页表项权限位、用户虚拟地址到物理地址的转换，以及页在进程完整生命周期中的分配、映射、取消映射和释放。

本实验对应 `pgtbl` 分支，包含：

- 在 `USYSCALL` 映射只读共享页，使 `ugetpid()` 无须陷入内核。
- 实现 `vmprint()`，递归打印三级页表中的所有有效 PTE。
- 实现 `pgaccess()`，读取并清除 RISC-V PTE 的 Accessed 位。

## 2. 修改文件

| 文件 | 作用 |
| --- | --- |
| `kernel/proc.h`、`kernel/proc.c` | 为每个进程分配 `usyscall` 页，写入 PID，映射为用户只读，并在退出时取消映射和释放。 |
| `kernel/vm.c`、`kernel/defs.h` | 实现并声明递归页表打印函数，公开 `walk()` 供访问检测使用。 |
| `kernel/exec.c` | 在 PID 1 完成 `exec` 后打印页表。 |
| `kernel/riscv.h` | 定义硬件访问位 `PTE_A`。 |
| `kernel/sysproc.c` | 实现 `pgaccess` 的参数检查、PTE 遍历、位图构造、访问位清除和 `copyout`。 |
| `answers-pgtbl.txt` | 回答共享页可优化的调用及页表输出解释。 |
| `Makefile` | 编译 `pgtbltest` 并适配新版 RISC-V 工具链。 |
| `time.txt` | 记录实验投入时间。 |

## 3. Sv39 页表结构

Sv39 把虚拟地址划分为三级索引和页内偏移：

```text
| VPN[2] 9 位 | VPN[1] 9 位 | VPN[0] 9 位 | offset 12 位 |
```

每个页表页包含 512 个 64 位 PTE。`walk()` 先用 `VPN[2]` 查根页表，再用 `VPN[1]` 查中间页表，最后用 `VPN[0]` 定位叶子 PTE。`PTE2PA` 从 PTE 中提取物理页号并恢复为 4 KiB 对齐的物理地址。

## 4. 关键设计

### 4.1 共享只读 USYSCALL 页

`allocproc()` 为每个进程额外分配一页物理内存，页首保存 `struct usyscall { int pid; }`。`proc_pagetable()` 将该页映射到固定虚拟地址 `USYSCALL`，权限为：

```c
PTE_R | PTE_U
```

存在 `PTE_U` 才允许用户态访问；只设置 `PTE_R` 而不设置 `PTE_W`，用户程序不能伪造 PID。`ugetpid()` 直接读取该地址，不执行 `ecall`，从而省去陷入、寄存器保存、分发和返回过程。

共享页必须像 `trapframe` 一样覆盖完整生命周期：分配物理页、建立映射、错误路径回收、取消映射、释放物理页。`exec()` 创建新页表时仍映射同一个进程的共享页；`fork()` 通过 `allocproc()` 为子进程创建包含新 PID 的独立共享页。

### 4.2 递归打印页表

`vmprintwalk()` 遍历页表页的 512 个 PTE，只输出设置 `PTE_V` 的条目。若有效 PTE 没有 `R/W/X` 任一权限，它不是叶子映射，而是指向下一级页表，因此继续递归；否则它是最终虚拟页到物理页的映射。

每深入一级增加一个 `" .."` 缩进，同时打印索引、完整 PTE 和 `PTE2PA(pte)`。在 `exec()` 中只对 PID 1 调用，避免每个程序启动都输出大量信息。

### 4.3 pgaccess 与 PTE_A

RISC-V 硬件在访问某个虚拟页后设置其 PTE 的 A 位（第 6 位）。`pgaccess(base, npages, mask)` 对每一页调用 `walk()`：

1. 检查 PTE 存在且同时具有 `PTE_V`、`PTE_U`。
2. 若 `PTE_A` 已设置，则把结果位图的第 `i` 位置 1。
3. 清除 `PTE_A`，使下一次调用只反映此后发生的新访问。
4. 使用 `copyout()` 把 32 位位图安全写入用户地址空间。

实现把最大页数限制为 32，与 `unsigned int` 位图宽度一致，并检查页数、地址上界和加法溢出，防止 `walk()` 收到超出 `MAXVA` 的地址。

## 5. 页表输出解释

PID 1 的低地址页通过根条目 0 到达：页 0 是 init 的代码和数据；页 1 是清除了 `PTE_U` 的栈保护页，用户态不能读写；页 2 是可读写的用户栈。

最高地址附近的叶子页依次包括：

- 509：`USYSCALL`，用户只读。
- 510：`TRAPFRAME`，仅 supervisor 可读写。
- 511：`TRAMPOLINE`，仅 supervisor 可读和执行。

`TRAMPOLINE` 在所有进程中位于相同虚拟地址，切换页表的陷阱代码才能连续执行。

## 6. 测试结果

执行：

```bash
make grade
```

实际结果：

```text
pgtbltest: ugetpid: OK
pgtbltest: pgaccess: OK
pte printout: OK
answers-pgtbl.txt: OK
usertests: all tests: OK
time: OK
Score: 46/46
```

完整 `usertests` 通过，说明新增映射没有破坏 `fork`、`exec`、`sbrk`、地址检查或进程退出时的内存回收。

## 7. 答辩高频问题与参考回答

### Q1：页表为什么需要三级，而不是一个大数组？

用户地址空间通常很稀疏。多级页表只为实际使用的地址区间分配下级页表页，节省大量内存；代价是地址转换需要多次查表，硬件用 TLB 缓存结果降低开销。

### Q2：PTE_V 和 PTE_U 分别控制什么？

`PTE_V` 表示页表项有效；`PTE_U` 表示用户模式允许访问。没有 `PTE_U` 的页仍可存在于用户页表中，但用户态访问会触发页故障，例如栈保护页、trapframe 和 trampoline。

### Q3：为什么 USYSCALL 不能设置 PTE_W？

该页的数据由内核维护。如果用户可写，就能修改 PID 等受信任信息，后续扩展还可能破坏安全判断。用户只需要读取，所以权限遵循最小授权原则。

### Q4：ugetpid 为什么比 getpid 快？

`getpid` 需要 `ecall`、特权级切换、保存和恢复寄存器以及系统调用分发；`ugetpid` 只执行普通的用户内存读取。不过共享值的维护和一致性由内核负责。

### Q5：哪些系统调用也可用共享页优化？

适合频繁读取、数据很小、无副作用且对当前进程可公开的值，例如父 PID、时钟 tick 和只读进程统计。不适合需要权限检查、阻塞、修改内核状态或返回变长数据的调用。

### Q6：如何区分页表的叶子 PTE 与中间 PTE？

有效 PTE 若 `R/W/X` 均为 0，则指向下一级页表；任一位为 1 则表示叶子映射。代码据此决定继续递归还是停止。

### Q7：为什么 pgaccess 读取后必须清除 PTE_A？

A 位一旦由硬件置 1 会一直保持。若不清除，后续调用只能知道“历史上访问过”，无法判断“自上次检查以来是否访问”。

### Q8：为什么结果先放在内核变量，再 copyout？

用户指针属于用户页表，内核不能信任或直接解引用。先在内核构造完整结果，再由 `copyout` 检查映射并复制，可安全处理非法地址。

### Q9：为什么取消映射时 `do_free` 传 0？

`USYSCALL`、`TRAPFRAME` 和 `TRAMPOLINE` 的物理页由其他明确路径管理，页表拆除只移除映射。若 `uvmunmap` 同时释放，可能与 `freeproc` 重复释放；trampoline 更是所有进程共享的内核代码。

### Q10：TLB 的作用是什么？

TLB 缓存最近的虚拟页到物理页转换及权限，避免每次内存访问都遍历三级页表。页表映射改变后通常需要 `sfence.vma` 使旧缓存失效；xv6 的用户态返回路径会在切换页表后执行刷新。

## 8. 现场演示建议

```bash
make qemu
```

启动时先解释 PID 1 的 `vmprint` 输出，再在 xv6 shell 运行：

```text
pgtbltest
```

指出 `ugetpid_test: OK` 验证共享只读页，`pgaccess_test: OK` 验证硬件访问位。最后运行 `make grade` 展示 `Score: 46/46`。
