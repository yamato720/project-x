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

## 文档目录

怎么跑和调试：

- [运行已有xclbin与host计时_zh.md](/home/pyx/ProjectFS/Project-X/docs/运行已有xclbin与host计时_zh.md)
- [Vivado设计查看教程_zh.md](/home/pyx/ProjectFS/Project-X/docs/Vivado设计查看教程_zh.md)

代码研究：

- [C层到硬件实现_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md)
- [C层到硬件实现_图解版_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md)
- [C++模板与make_bo详解_zh.md](/home/pyx/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md)

硬件接口与资源：

- [AXI与控制寄存器_zh.md](/home/pyx/ProjectFS/Project-X/docs/AXI与控制寄存器_zh.md)
- [BO与HBM映射_zh.md](/home/pyx/ProjectFS/Project-X/docs/BO与HBM映射_zh.md)
- [HLS pragma到U55C硬件对照_zh.md](</home/pyx/ProjectFS/Project-X/docs/HLS pragma到U55C硬件对照_zh.md>)

Chisel 与 HLS 集成：

- [HLS与Chisel取舍_zh.md](/home/pyx/ProjectFS/Project-X/docs/HLS与Chisel取舍_zh.md)
- [Chisel_FP64_IP接入HLS_zh.md](/home/pyx/ProjectFS/Project-X/docs/Chisel_FP64_IP接入HLS_zh.md)
- [hardware/chisel/README.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/README.md)
- [hardware/chisel/安装与版本切换_zh.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/安装与版本切换_zh.md)

## 构建

软件仿真：

```bash
cd /home/pyx/ProjectFS/Project-X
make run-sw ROWS=8 SCALE=2 X0=1
```

只运行已有软件仿真产物，不触发 xclbin 重新构建：

```bash
cd /home/pyx/ProjectFS/Project-X
make run-sw-existing ROWS=8 SCALE=2 X0=1
```

硬件 bitstream：

```bash
cd /home/pyx/ProjectFS/Project-X
make build TARGET=hw
```

只运行已有硬件 xclbin，不触发硬件重新构建：

```bash
cd /home/pyx/ProjectFS/Project-X
make run-hw-existing ROWS=8 SCALE=2 X0=1
```

host 侧计时：

```bash
cd /home/pyx/ProjectFS/Project-X
make run-hw-existing ROWS=8 SCALE=2 X0=1 TIMING=1 WARMUP=1 REPEAT=5
```

生成 Chisel Verilog：

```bash
cd /home/pyx/ProjectFS/Project-X
make chisel
```

打开 Vivado 工程或最终 routed checkpoint：

```bash
cd /home/pyx/ProjectFS/Project-X
make vivado-open
make vivado-routed
```

默认 Vivado GUI 缩放是 200%。如果需要改成 100%：

```bash
make vivado-open VIVADO_SCALE=1
```

如果要通过 SSH 下载到本地 Vivado 查看，优先下载 `level0_wrapper_routed.dcp` 和 `reports/hw/...`；如果要看完整 Vitis/Vivado 工程结构，下载整个 `build/hw/.../_x_temp/link/vivado/vpl/`。服务器端可用 `make vivado-package` / `make vivado-package-full` 打包，Linux/macOS 客户端可用 [download-vivado-view.sh](/home/pyx/ProjectFS/Project-X/scripts/download-vivado-view.sh)，Windows 客户端可用 [download-vivado-view.ps1](/home/pyx/ProjectFS/Project-X/scripts/download-vivado-view.ps1)。细节见：[Vivado设计查看教程_zh.md](/home/pyx/ProjectFS/Project-X/docs/Vivado设计查看教程_zh.md)

## 当前 HLS 输出

当前 `hardware/krnl_spmv.cpp` 会输出两份结果：

- `y = scale * A * x`
- `yy_t = y * y^T`

这里的 `yy_t` 是完整外积矩阵，不是只算上三角。  
因为输出规模是 `rows * rows`，host 侧目前把 `rows` 限制在 `512` 以内。

## 当前 Chisel 范围

`hardware/chisel` 当前使用 Chisel `7.11.0`、Scala `2.13.18`、sbt `1.11.7`，默认通过 OpenJDK 17 生成。

`hardware/chisel` 里现在有这些模块：

- `MergeReducer`
  把多个 lane 的部分结果归并求和。
- `SpmvRowEngine`
  生成单行 `SpMV` 的乘加骨架，并复用 `MergeReducer` 做行内 reduction。
- `OuterProductMul`
  为 HLS 外积阶段生成 RTL black-box wrapper、C model、JSON 描述和 Vivado FP64 multiply IP，让 `make run-sw` 在构建 `krnl_spmv.xo` 前先跑 Chisel/Vivado 生成。

当前实现是整数 datapath 结构模板，不是 FP64 等价实现。  
`OuterProductMul` 已经把外积乘法换成 Xilinx Floating Point FP64 IP；软件仿真路径仍保留 C++ fallback/model 保持 `double` 结果一致。

## 一个关键澄清

`SpMV` 输出的是 `y = A * x`，这本身不能直接推出矩阵行列式。  
归并模块适合做部分和归约、dot-product 汇总、stream merge；它不能单独替代矩阵分解算法。
