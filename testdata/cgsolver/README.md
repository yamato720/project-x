# CG Solver Testdata

这套测试数据用于在 `testdata/cgsolver` 下做一个独立的 CG solver 验证链路：

- Python 生成 CSR 格式的 SPD 稀疏矩阵数据集
- C++ 程序读取数据集，按 Jacobi-PCG 伪代码执行求解
- 用数据集里的 `x_expected` 和残差阈值做校验

它是纯 CPU 层面的算法测试，不依赖 HLS、kernel、XRT 或现有 FPGA 构建流程。

为了更适合作为 HLS 设计参考，求解器已经按“初始化 + 循环阶段”拆成多个独立类，
并分别放在 `include/cgsolver/*.hpp` 中，而不是写成一个大函数。

算法说明文档见：

- [docs/Jacobi-PCG算法原理与流程_zh.md](/home/pyx/ProjectFS/Project-X/testdata/cgsolver/docs/Jacobi-PCG算法原理与流程_zh.md)

默认问题规模是 `512`，也支持在生成和测试时指定其他规模。

## 目录

```text
testdata/cgsolver/
  docs/
    Jacobi-PCG算法原理与流程_zh.md
  include/cgsolver/
    Dataset.hpp
    SparseMatrixCsr.hpp
    JacobiPreconditioner.hpp
    SolverState.hpp
    InitializeProblem.hpp
    ComputeApStage.hpp
    ComputeAlphaStage.hpp
    UpdateXStage.hpp
    UpdateResidualStage.hpp
    ApplyPreconditionerStage.hpp
    UpdateDirectionStage.hpp
    UpdateResidualNormStage.hpp
    JacobiPcgSolver.hpp
  generate_dataset.py
  cgsolver_verify.cpp
  Makefile

testdata/generated/
  cgsolver/
    datasets/
      n512/
        meta.txt
        row_ptr.txt
        col_idx.txt
        values.txt
        jacobi_diag.txt
        rhs.txt
        x0.txt
        x_expected.txt
```

## 数据格式

- `meta.txt`
  保存 `n`、`nnz`、`max_iters`、`tau`、`check_tolerance`
- `row_ptr.txt`
  矩阵 `A` 的 CSR `row_ptr`，长度 `n + 1`
- `col_idx.txt`
  矩阵 `A` 的 CSR `col_idx`，长度 `nnz`
- `values.txt`
  矩阵 `A` 的 CSR `values`，长度 `nnz`
- `jacobi_diag.txt`
  Jacobi 预条件器 `M` 的对角线，也就是 `diag(A)`
- `rhs.txt`
  右端项向量 `b`
- `x0.txt`
  初始解向量 `x0`
- `x_expected.txt`
  参考解向量 `x`

当前生成器输出的是一个三对角 SPD 矩阵：

```text
A[i, i]     = 4 + 0.25 * cos(0.1 * i)
A[i, i - 1] = -1
A[i, i + 1] = -1
```

并使用：

- `M = diag(A)`
- 一个确定性的 `x_expected`
- 一个确定性的非零初始向量 `x0`

来构造输入和校验目标。也就是说，验证程序的输入与伪代码一致：

1. `A`
2. `M`
3. `b`
4. `x0`
5. `tau`
6. `Nmax`

然后再额外用 `x_expected` 做离线正确性校验。

## 伪代码对应关系

顶层装配在 `cgsolver_verify.cpp`，求解主流程在 `JacobiPcgSolver.hpp`。

各阶段与伪代码的对应关系如下：

1. `InitializeProblem.hpp`
   实现 `r <- b - A x0`、`z <- M^{-1} r`、`p <- z`、`rz <- r^T z`、`rr <- r^T r`
2. `ComputeApStage.hpp`
   实现 `ap <- A p`
3. `ComputeAlphaStage.hpp`
   实现 `alpha <- rz / (p^T ap)`
4. `UpdateXStage.hpp`
   实现 `x <- x + alpha p`
5. `UpdateResidualStage.hpp`
   实现 `r <- r - alpha ap`
6. `ApplyPreconditionerStage.hpp`
   实现 `z <- M^{-1} r`
7. `UpdateDirectionStage.hpp`
   实现 `rz_new <- r^T z` 和 `p <- z + (rz_new / rz) p`
8. `UpdateResidualNormStage.hpp`
   实现 `rr <- r^T r`

其中：

- `SparseMatrixCsr.hpp` 负责稀疏矩阵 `A` 的 CSR `SpMV`
- `JacobiPreconditioner.hpp` 负责 Jacobi 预条件步骤
- `SolverState.hpp` 负责保存 `x / r / z / p / ap / rz / rr`

## 使用

生成默认 512 数据集：

```bash
make -C testdata/cgsolver generate
```

生成自定义规模，例如 1024：

```bash
make -C testdata/cgsolver generate SIZE=1024
```

如需自定义 `tau` 或最大迭代次数：

```bash
make -C testdata/cgsolver generate SIZE=1024 TAU=1e-18 MAX_ITERS=4096
```

编译验证程序：

```bash
make -C testdata/cgsolver build
```

生成并验证默认 512：

```bash
make -C testdata/cgsolver test
```

生成并验证自定义规模：

```bash
make -C testdata/cgsolver test SIZE=1024
```
