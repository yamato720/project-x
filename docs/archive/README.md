# Project-X 归档文档

这里收纳的是旧版本说明文档。它们没有删除，是因为仍然保留了一些背景解释和历史排查记录；但它们默认不再作为当前工程入口文档使用。

这些文档之所以归档，通常有一个或多个原因：

- 基于旧的单文件内核路径 `hardware/krnl_spmv.cpp`
- 默认使用旧的 `build/hw/...`、`build/sw_emu/...`、`reports/hw/...` 布局
- 假设工程只有一个主要 kernel 版本，而不是 `hls` / `hybrid` / `chisel_core` 三变体
- 没有覆盖当前的顺序脚本、分 variant 打包和分 variant 运行入口

当前仍推荐优先阅读主目录下的：

- [../构建变体_zh.md](~/ProjectFS/Project-X/docs/构建变体_zh.md)
- [../运行已有xclbin与host计时_zh.md](~/ProjectFS/Project-X/docs/运行已有xclbin与host计时_zh.md)
- [../HLS外壳_Chisel计算核_zh.md](~/ProjectFS/Project-X/docs/HLS外壳_Chisel计算核_zh.md)

本目录当前包含：

- [AXI与控制寄存器_zh.md](~/ProjectFS/Project-X/docs/archive/AXI与控制寄存器_zh.md)
- [Chisel_FP64_IP接入HLS_zh.md](~/ProjectFS/Project-X/docs/archive/Chisel_FP64_IP接入HLS_zh.md)
- [C层到硬件实现_zh.md](~/ProjectFS/Project-X/docs/archive/C层到硬件实现_zh.md)
- [C层到硬件实现_图解版_zh.md](~/ProjectFS/Project-X/docs/archive/C层到硬件实现_图解版_zh.md)
- [HLS pragma到U55C硬件对照_zh.md](<~/ProjectFS/Project-X/docs/archive/HLS pragma到U55C硬件对照_zh.md>)
- [Vivado设计查看教程_zh.md](~/ProjectFS/Project-X/docs/archive/Vivado设计查看教程_zh.md)
