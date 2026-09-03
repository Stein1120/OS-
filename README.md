# xv6-labs-2021 课程设计

本项目基于 MIT xv6-labs-2021，在 WSL2 Ubuntu 环境中完成 10 个实验，并通过各分支官方评分脚本。累计成绩为 **846/846**。

仓库地址：<https://github.com/Stein1120/OS->

## 实验与分支

| Lab | 分支 | 实现提交 | 官方评分 |
|---|---|---:|---:|
| Utilities | `util` | `fcfcd50` | 100/100 |
| System calls | `syscall` | `6626d65` | 35/35 |
| Page tables | `pgtbl` | `7220fdd` | 46/46 |
| Traps | `traps` | `710a30f` | 85/85 |
| Copy-on-Write | `cow` | `68cd25b` | 110/110 |
| Multithreading | `thread` | `4b6a109` | 60/60 |
| Network driver | `net` | `224a118` | 100/100 |
| Locks | `lock` | `18d0c70` | 70/70 |
| File system | `fs` | `3ad16ac` | 100/100 |
| mmap | `mmap` | `893f75d` | 140/140 |

## 环境

- Windows 11 + WSL2 2.7.12
- Ubuntu 26.04
- GCC / `riscv64-linux-gnu-gcc` 15.2.0
- GNU Binutils 2.46
- GDB 17.1
- QEMU 10.2.1
- GNU Make 4.4.1
- Python 3.14

为兼容新版工具链，各实验分支统一显式使用 `-march=rv64gc -mabi=lp64`，并对旧版评分脚本及严格编译告警做了最小兼容调整；实验语义未改变。

## 运行与评分

```bash
git switch <branch>
make clean
make qemu
```

退出 QEMU 后执行对应实验的官方脚本，例如：

```bash
./grade-lab-cow
./grade-lab-net
./grade-lab-lock
./grade-lab-fs
./grade-lab-mmap
```

其余分支可使用 `make grade` 或该分支自带的 `grade-lab-*` 脚本。

## File System Lab 说明

`fs` 分支会生成约 200 MB 的 xv6 文件系统镜像。在 WSL 的 `/mnt/c` 路径下，大量小块 I/O 经过 NTFS 转发可能导致 `bigfile` 超时。将同一份源码复制到 WSL 原生 ext4 目录后，官方测试完整通过并获得 100/100。这是宿主文件系统性能差异，不是块映射逻辑错误。

## 项目文档

课程设计报告包含环境搭建、每个 Lab 的目标、实现过程、问题与解决方法、测试结果、实验心得和答辩问答。MIT 原始项目说明保留在根目录的 `README` 文件中。
