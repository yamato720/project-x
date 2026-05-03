# Project-X 文档索引

这里收纳 `Project-X` 的中文说明文档。根目录 README 保留快速入口；这里按用途细分。

## 快速运行与调试

- [运行已有xclbin与host计时_zh.md](/home/pyx/ProjectFS/Project-X/docs/运行已有xclbin与host计时_zh.md)
  说明 `run-sw-existing` / `run-hw-existing`、host 计时、warmup/repeat。
- [Vivado设计查看教程_zh.md](/home/pyx/ProjectFS/Project-X/docs/Vivado设计查看教程_zh.md)
  说明 `make vivado-open` / `make vivado-routed`、X11 GUI 缩放、层级/资源/布局/时序怎么看，以及如何用 SSH 下载到 Linux/Windows/macOS 客户端。

## 代码研究

- [C层到硬件实现_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md)
  从 C/C++ kernel 看它如何落到硬件结构。
- [C层到硬件实现_图解版_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md)
  图解版数据路径说明。
- [C++模板与make_bo详解_zh.md](/home/pyx/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md)
  解释 host 侧 `make_bo`、模板和 XRT BO 映射。

## 硬件接口与资源

- [AXI与控制寄存器_zh.md](/home/pyx/ProjectFS/Project-X/docs/AXI与控制寄存器_zh.md)
  解释 AXI-Lite 控制寄存器、kernel 启动和参数传递。
- [BO与HBM映射_zh.md](/home/pyx/ProjectFS/Project-X/docs/BO与HBM映射_zh.md)
  解释 host BO、kernel m_axi 端口和 U55C HBM bank 映射。
- [HLS pragma到U55C硬件对照_zh.md](</home/pyx/ProjectFS/Project-X/docs/HLS pragma到U55C硬件对照_zh.md>)
  对照 HLS pragma 和最终 U55C 硬件结构/资源影响。

## Chisel 与 HLS 集成

- [HLS与Chisel取舍_zh.md](/home/pyx/ProjectFS/Project-X/docs/HLS与Chisel取舍_zh.md)
  说明哪些逻辑适合 HLS，哪些适合 Chisel/RTL。
- [Chisel_FP64_IP接入HLS_zh.md](/home/pyx/ProjectFS/Project-X/docs/Chisel_FP64_IP接入HLS_zh.md)
  说明 Chisel wrapper、HLS black-box、Vivado FP64 multiply IP 如何接入外积阶段。
- [hardware/chisel/README.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/README.md)
  Chisel 子工程说明。
- [hardware/chisel/安装与版本切换_zh.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/安装与版本切换_zh.md)
  Chisel 7.11.0、sbt、OpenJDK 17+ 安装和版本切换说明。

## 当前设计边界

`SpMV` 的输出 `y = A * x` 不能直接推出矩阵行列式。归并模块适合做的是部分乘加结果汇总、reduction tree、stream merge，不是替代 `LU/QR/Cholesky` 这类行列式或分解算法。
