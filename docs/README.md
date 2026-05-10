# Project-X 文档索引

这里收纳 `Project-X` 的中文说明文档。根目录 README 保留快速入口；这里按用途细分。

## 快速运行与调试

- [构建变体_zh.md](~/ProjectFS/Project-X/docs/构建变体_zh.md)
  说明 `hls` / `hybrid` / `chisel_core` 三种 kernel 变体的构建、产物路径、顺序脚本和测试入口。
- [运行已有xclbin与host计时_zh.md](~/ProjectFS/Project-X/docs/运行已有xclbin与host计时_zh.md)
  说明 `run-sw-existing` / `run-hw-existing`、host 计时、warmup/repeat。

## 子工程

- [Project-XPlus/README.md](~/ProjectFS/Project-X/Project-XPlus/README.md)
  `Project-XPlus` 是 Jacobi-PCG 多 kernel HLS/XRT 子工程，已作为独立 submodule 接入。

## 代码研究

- [C++模板与make_bo详解_zh.md](~/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md)
  解释 host 侧 `make_bo`、模板和 XRT BO 映射。

## 硬件接口与资源

- [BO与HBM映射_zh.md](~/ProjectFS/Project-X/docs/BO与HBM映射_zh.md)
  解释 host BO、kernel m_axi 端口和 U55C HBM bank 映射。

## Chisel 与 HLS 集成

- [HLS与Chisel取舍_zh.md](~/ProjectFS/Project-X/docs/HLS与Chisel取舍_zh.md)
  说明哪些逻辑适合 HLS，哪些适合 Chisel/RTL。
- [HLS外壳_Chisel计算核_zh.md](~/ProjectFS/Project-X/docs/HLS外壳_Chisel计算核_zh.md)
  说明 HLS 只做 XRT/AXI 接口、Chisel 接管 compute pipeline 的混合架构边界和落地步骤。
- [外积Tile优化收益与后续方向_zh.md](~/ProjectFS/Project-X/docs/外积Tile优化收益与后续方向_zh.md)
  总结这次 `outer_product_tile_bits` 外积 tile 化为什么能带来 `2x+` 提升，以及后续继续提速的优先方向。
- [多BlackBox共存问题_zh.md](~/ProjectFS/Project-X/docs/多BlackBox共存问题_zh.md)
  总结 `chisel_core` 下两个 RTL black-box 共存时，floating-point 依赖在 HLS/VPL 导出链路里的问题与修法。
- [hardware/chisel/README.md](~/ProjectFS/Project-X/hardware/chisel/README.md)
  Chisel 子工程说明。
- [hardware/chisel/安装与版本切换_zh.md](~/ProjectFS/Project-X/hardware/chisel/安装与版本切换_zh.md)
  Chisel 7.11.0、sbt、OpenJDK 17+ 安装和版本切换说明。

## 归档文档

- [archive/README.md](~/ProjectFS/Project-X/docs/archive/README.md)
  收纳基于旧内核单文件布局、旧 build/reports 路径和旧说明口径的历史文档。

## 当前设计边界

`SpMV` 的输出 `y = A * x` 不能直接推出矩阵行列式。归并模块适合做的是部分乘加结果汇总、reduction tree、stream merge，不是替代 `LU/QR/Cholesky` 这类行列式或分解算法。
