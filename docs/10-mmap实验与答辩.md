# Lab 10：mmap 实验与答辩记录

## 1. 实验目标

本实验为 xv6 实现文件支持的内存映射，使用户进程能够通过虚拟地址直接访问文件内容。重点不是简单复制文件，而是建立 VMA 元数据，在页故障时按需分配物理页，并正确处理权限、共享回写、局部解除映射、进程复制与退出清理。

本实验对应 `mmap` 分支，包含：

- 新增 `mmap`、`munmap` 系统调用及用户态接口。
- 每进程维护固定数量的 VMA，映射区从高地址向下分配。
- 处理指令、读、写页故障并从文件延迟装页。
- 区分 `MAP_PRIVATE` 与 `MAP_SHARED`，共享映射在解除时回写。
- 让 VMA 正确参与 `fork`、`exit` 和 `exec` 生命周期。

## 2. 修改文件

| 文件 | 作用 |
| --- | --- |
| `kernel/param.h`、`kernel/proc.h` | 定义每进程 16 个 VMA 和高地址分配边界。 |
| `kernel/sysfile.c` | 实现映射、解除映射、缺页装入、回写和 fork 复制。 |
| `kernel/trap.c` | 把页故障分派给 VMA 处理器。 |
| `kernel/proc.c` | 初始化 VMA，在 fork/exit 中复制或释放。 |
| `kernel/exec.c` | 成功替换地址空间前清理旧 VMA。 |
| `kernel/defs.h` | 声明 VMA 辅助函数和页表遍历接口。 |
| `kernel/syscall.h`、`kernel/syscall.c` | 注册两个系统调用。 |
| `user/user.h`、`user/usys.pl` | 提供用户态调用桩。 |
| `Makefile` | 把 `mmaptest` 加入镜像并适配工具链。 |
| `gradelib.py`、`user/usertests.c` | 适配新版 Python/GCC。 |
| `time.txt` | 记录实验用时。 |

## 3. VMA 数据结构与地址布局

每个 VMA 保存：

```text
used, addr, length, prot, flags, file offset, file pointer
```

进程固定拥有 16 个槽，避免在教学内核中引入通用内核堆分配器。`mmap()` 查找空闲槽，并通过 `filedup()` 保存独立文件引用，所以用户随后 `close(fd)` 或 `unlink(path)` 都不会使映射失效。

本实现不增加 `p->sz`，而是让 `mmap_top` 从 `TRAPFRAME` 下方向低地址递减：

```text
低地址：代码 / 数据 / 堆  -> 向上增长
高地址：mmap 区            -> 向下增长
顶部：TRAPFRAME / TRAMPOLINE
```

这样普通用户内存仍是连续的 `0..p->sz`，现有 `uvmcopy()` 和 `uvmfree()` 无需接受因延迟映射产生的页表空洞。分配前检查新映射不会越过 `PGROUNDUP(p->sz)`。

## 4. mmap 参数与权限检查

实验接口要求地址提示为 0，由内核选择地址。实现还检查：

- 长度大于 0，文件偏移非负且页对齐。
- `prot` 只能包含 READ、WRITE、EXEC。
- `flags` 必须恰为 `MAP_SHARED` 或 `MAP_PRIVATE`。
- 文件描述符存在、类型为 inode 且可读。
- 若共享映射要求写权限，文件本身必须可写。

只读文件允许创建可写的 `MAP_PRIVATE` 映射，因为修改仅存在于该进程私有物理页，不会写回文件。

## 5. 页故障按需装入

`mmap` 只登记 VMA，不分配物理页。当用户首次读取、写入或执行映射地址时，RISC-V 分别产生 scause 13、15 或 12。处理流程为：

1. 用 `stval` 找到包含故障地址的 VMA。
2. 根据故障类型检查 `PROT_READ/WRITE/EXEC`。
3. 向下取整得到页地址，并确认它尚未映射。
4. `kalloc()` 分配一页并清零。
5. 锁定 VMA 持有的 inode，从 `offset + (va - vma.addr)` 读取一页。
6. 根据 VMA 权限构造 PTE 并调用 `mappages()`。

文件末尾不足一页时，`readi()` 只复制现有字节；其余部分因预先清零而为零。只有实际访问的页才占用物理内存，体现 demand paging。

## 6. munmap 与共享回写

实验允许解除整个 VMA、前缀或后缀，不要求在中间打洞。`munmap()` 先验证范围属于同一个 VMA，再逐页处理：

- 页从未访问、页表项不存在：直接跳过，仍算解除成功。
- `MAP_PRIVATE`：释放物理页，不写文件。
- 可写 `MAP_SHARED`：在释放前把页内属于现有文件大小的部分写回。

回写时每页使用一个 `begin_op()/end_op()` 事务并持有 inode 睡眠锁。映射超过 EOF 的清零区域不用于扩展文件。解除前缀时同步增加 VMA 起始地址和文件偏移；解除后缀只缩短长度；解除全部还要 `fileclose()` 并清空槽。

本实验允许把所有驻留共享页都写回，无需依赖硬件 dirty 位；结果正确，只是可能多做一次写盘。

## 7. fork、exit 与 exec

### 7.1 fork

子进程复制每个 VMA 的元数据，并对文件执行 `filedup()`。父进程已经装入的映射页被复制到新的物理页，尚未装入的页仍保持延迟状态，子进程首次访问时自行从文件加载。

实验允许 `MAP_SHARED` 的父子映射不共享同一物理页；各自解除映射时都可写回文件。复制失败时，错误路径解除已建立的子映射、释放物理页并撤销文件引用，避免泄漏。

### 7.2 exit

进程退出前对所有 VMA 执行完整 `munmap` 语义：共享页先回写，所有页释放，所有 VMA 文件引用关闭。随后才关闭普通文件描述符并进入僵尸状态。

### 7.3 exec

`exec` 只有在新 ELF 地址空间完全建立成功后，才清理旧 VMA并替换页表。若新程序加载失败，旧映射仍可继续使用；成功后旧高地址叶 PTE 已移除，`freewalk` 不会因残留映射而 panic。

## 8. 测试结果

执行：

```bash
./grade-lab-mmap
```

实际结果：

```text
mmaptest: mmap f: OK
mmaptest: mmap private: OK
mmaptest: mmap read-only: OK
mmaptest: mmap read/write: OK
mmaptest: mmap dirty: OK
mmaptest: not-mapped unmap: OK
mmaptest: two files: OK
mmaptest: fork_test: OK
usertests: OK
time: OK
Score: 140/140
```

## 9. 答辩高频问题与参考回答

### Q1：mmap 和 read 的主要区别是什么？

`read` 由系统调用显式把文件数据复制到用户缓冲区并更新文件偏移；`mmap` 建立文件与虚拟地址区间的关联，用户以普通内存指令访问，内核通过缺页异常按需装入。

### Q2：为什么 mmap 时不立即读取全部文件？

延迟装页只为实际访问的部分分配内存和执行 I/O，可减少启动延迟、物理内存占用和无用磁盘读取，也是虚拟内存的核心优势。

### Q3：VMA 和 PTE 分别记录什么？

VMA 是较高层的连续区域描述，保存文件、偏移、长度和权限；PTE 是逐页硬件映射，保存虚拟页到物理页及 R/W/X/U 权限。未装入的 VMA 合法存在，但对应 PTE 尚无效。

### Q4：为什么映射要持有独立 file 引用？

文件描述符和映射生命周期独立。程序可以 mmap 后立即 close，甚至 unlink 路径；VMA 的 `filedup` 引用使 inode 在最后一个映射解除前仍然存在。

### Q5：MAP_PRIVATE 为什么能映射只读打开的文件为可写？

写操作只修改进程自己的物理页，解除映射时不回写文件，因此不需要文件写权限。它本质上是文件初始化的私有内存副本。

### Q6：MAP_SHARED 为什么要求文件可写？

共享可写映射的修改必须传播到文件。若描述符没有写权限却允许回写，就绕过了文件访问控制。

### Q7：为什么未访问的页也能 munmap 成功？

VMA 声明的是虚拟区间，物理页只是按需存在。解除映射应更新区域生命周期；没有 PTE 代表无需释放物理页，而不是地址无效。

### Q8：如何确定页在文件中的偏移？

`file_offset = vma.offset + (page_va - vma.addr)`。解除 VMA 前缀时，虚拟起点和文件偏移必须同步增加，才能保持后续页对应关系。

### Q9：为什么不能把磁盘 I/O 放在自旋锁临界区？

磁盘 I/O 可能睡眠等待中断，而持有自旋锁时不能睡眠，否则其他 CPU 会持续空转甚至形成死锁。inode 和缓冲区内容使用允许睡眠的锁。

### Q10：fork 后 MAP_SHARED 为什么没有共享物理页仍能通过？

实验允许父子各有物理副本；两者仍指向同一文件 inode，解除映射时写回即可体现文件级共享。真实系统通常共享页缓存物理页，语义更及时、效率更高。

### Q11：为什么 exec 也必须清理 VMA？

`exec` 替换整个用户地址空间。旧 VMA 的物理页、PTE 和文件引用若不释放会泄漏；高地址叶 PTE残留还会让递归释放页表时触发 `freewalk: leaf`。

### Q12：本实现为什么从高地址向下放置 VMA？

它把映射区与从零开始的代码/堆分离，不改变 `p->sz`，因此普通地址空间复制和释放逻辑保持连续模型。代价是当前实现保守地不回收 `mmap_top` 空洞，但地址空间足够大且同时 VMA 有上限。

### Q13：怎样保证权限错误不会被误当成可修复缺页？

处理器报告故障类型后，VMA 处理器先检查对应 PROT 位；若页已存在却仍故障，说明是权限问题，也直接失败并终止进程，不会重新映射为更宽权限。

## 10. 现场演示建议

运行：

```bash
./grade-lab-mmap
```

代码讲解按“`sys_mmap` 只登记 VMA → 用户访问触发 `usertrap` → `vma_pagefault` 分配和读文件 → `sys_munmap` 共享回写”的链路展开。随后说明 `fork` 复制已驻留页、`exit/exec` 统一释放，最后展示 `Score: 140/140`。
