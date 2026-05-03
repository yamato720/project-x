# Project-X 文档索引

这里收纳 `Project-X` 的中文说明文档，根目录只保留总览入口和构建说明。

## 架构与数据路径

- [C层到硬件实现_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md)
- [C层到硬件实现_图解版_zh.md](/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md)
- [AXI与控制寄存器_zh.md](/home/pyx/ProjectFS/Project-X/docs/AXI与控制寄存器_zh.md)
- [BO与HBM映射_zh.md](/home/pyx/ProjectFS/Project-X/docs/BO与HBM映射_zh.md)

## 代码与实现细节

- [C++模板与make_bo详解_zh.md](/home/pyx/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md)
- [HLS pragma到U55C硬件对照_zh.md](</home/pyx/ProjectFS/Project-X/docs/HLS pragma到U55C硬件对照_zh.md>)
- [HLS与Chisel取舍_zh.md](/home/pyx/ProjectFS/Project-X/docs/HLS与Chisel取舍_zh.md)

## Chisel 说明

- [hardware/chisel/README.md](/home/pyx/ProjectFS/Project-X/hardware/chisel/README.md)

补充一点：`SpMV` 的输出 `y = A * x` 不能直接推出矩阵行列式。  
归并模块适合做的是“部分乘加结果汇总 / reduction tree / stream merge”，不是替代 `LU/QR/Cholesky` 这类行列式或分解算法。
