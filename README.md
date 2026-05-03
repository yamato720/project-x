# Project-X

`Project-X` 现在分成三块：

- `software/`
  host/XRT 程序。
- `hardware/`
  HLS kernel 和 Chisel 模块。
- `docs/`
  中文讲解文档与索引。

## 目录

```text
Project-X/
  Makefile
  cfg/
  docs/
  hardware/
    chisel/
    krnl_spmv.cpp
  software/
    host.cpp
```

## 快速入口

- 文档索引：[docs/README.md](/home/pyx/ProjectFS/Project-X/docs/README.md)
- HLS kernel：[hardware/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/krnl_spmv.cpp)
- Chisel 模块说明：[hardware/chisel/README.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/README.md)
- host 程序：[software/host.cpp](/home/pyx/ProjectFS/Project-X/software/host.cpp)

## 构建

软件仿真：

```bash
cd /home/pyx/ProjectFS/Project-X
make run-sw ROWS=8 SCALE=2 X0=1
```

硬件 bitstream：

```bash
cd /home/pyx/ProjectFS/Project-X
make build TARGET=hw
```

生成 Chisel Verilog：

```bash
cd /home/pyx/ProjectFS/Project-X
make chisel
```

## 当前 Chisel 范围

`hardware/chisel` 里现在有两个结构模块：

- `MergeReducer`
  把多个 lane 的部分结果归并求和。
- `SpmvRowEngine`
  生成单行 `SpMV` 的乘加骨架，并复用 `MergeReducer` 做行内 reduction。

当前实现是整数 datapath 结构模板，不是 FP64 等价实现。  
如果你要和 HLS `double` kernel 严格对齐，下一步应补浮点运算单元。

## 一个关键澄清

`SpMV` 输出的是 `y = A * x`，这本身不能直接推出矩阵行列式。  
归并模块适合做部分和归约、dot-product 汇总、stream merge；它不能单独替代矩阵分解算法。
