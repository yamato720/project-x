# U55C 上 Jacobi-PCG 多 Kernel HLS 正式方案

日期：2026-05-10  
状态：v1 正式设计  
适用范围：`Project-X/Project-XS/data/generated/cgsolver/n512`

## 1. 文档目的

本文档定义 `Project-X` 中用于求解稀疏线性方程组 `A x = b` 的正式 HLS 方案。目标不是再给 Codex 一份“该做什么”的任务说明，而是明确：

1. 第一版可落地的硬件/软件边界
2. 必须实现的多 kernel 架构
3. 与当前仓库实际目录、数据集和 golden 参考一致的接口
4. `sw_emu -> hw_emu -> hw` 的实现顺序与验收标准

本文档替代此前偏任务分解风格的草稿说明，作为后续实现与评审的设计基线。

---

## 2. 已确认的仓库事实

本方案基于当前仓库中已经存在的内容，而不是抽象假设。

### 2.1 数据集现状

目标目录：

```text
Project-X/Project-XS/data/generated/cgsolver/n512
```

实际文件为：

```text
row_ptr.txt
col_idx.txt
values.txt
b.txt
x0.txt
```

当前 **没有** 单独的 `m_inv.txt`、`diag.txt`、`metadata.json` 或 `MatrixMarket` 文件。因此：

1. `A` 采用 CSR 三数组加载
2. `b` 和 `x0` 直接从文本向量读取
3. `Jacobi` 预条件器必须由 host 侧从 `diag(A)` 现场构造

### 2.2 数据集数值性质

`Project-X/Project-XS/script/generate_cg_dataset.py` 生成的是确定性的稀疏对称正定系统。对第一版 HLS 方案，这意味着：

1. 不需要在 kernel 内做复杂的 SPD 验证
2. 只需要在 host 侧完成基本输入校验、对角非零检查和 breakdown 防护

### 2.3 现有 golden 参考

当前仓库已有：

```text
Project-X/Project-XS/src/CgSolverGolden.hpp
Project-X/Project-XS/src/CsrDataset.hpp
```

其中：

1. `CsrDataset.hpp` 已实现 CSR 数据加载、SpMV、Jacobi 对角提取
2. `CgSolverGolden.hpp` 已实现 CPU 版 Jacobi-PCG
3. 当前 golden 使用 `double`

因此第一版 HLS 设计必须以该 golden 为数值参考，而不是另起一套算法定义。

### 2.4 与当前 Project-X 主工程的关系

当前 `Project-X` 根工程的 Makefile/host 流程是围绕单个 `krnl_spmv` demo 组织的，接口固定，主要用于变体比较：

```text
hls / hybrid / chisel_core
```

它并不直接适合承载一个新的多 kernel PCG solver。  
因此本方案的正式落地点不是复用当前 `krnl_spmv` 顶层，而是新增独立子工程。

---

## 3. 设计目标与非目标

## 3.1 设计目标

第一版必须满足以下目标：

1. 严格实现 **Jacobi-PCG**，不能退化成普通 CG
2. 采用 **多 kernel** 架构，而不是单一 monolithic solver kernel
3. 在 `n512` 数据集上可复现运行
4. 优先通过 `sw_emu` 与 `hw_emu`
5. 能与现有 CPU golden 做逐项对照
6. 保持结构清晰，便于后续扩展到更大 `n` 或多 CU SpMV

## 3.2 非目标

第一版 **不** 追求以下目标：

1. 不追求一上来接入当前 `Project-X` 根目录的三变体构建体系
2. 不追求多 CU SpMV 的最终高性能版本
3. 不追求 kernel 之间的 stream 直连 dataflow 化
4. 不追求对任意输入矩阵的鲁棒自适应支持

第一版的优先级是：**正确性 > 可编译性 > 可验证性 > 性能**。

---

## 4. 算法定义与数值约定

## 4.1 必须实现的算法

目标算法保持为：

```text
r  = b - A x0
z  = M^{-1} r
p  = z
rz = r^T z
rr = r^T r

for i = 0; i < Nmax && rr > tau; i++:
    ap     = A p
    alpha  = rz / (p^T ap)
    x      = x + alpha p
    r      = r - alpha ap
    z      = M^{-1} r
    rz_new = r^T z
    p      = z + (rz_new / rz) p
    rz     = rz_new
    rr     = r^T r
```

其中：

1. `M = diag(A)`
2. `M^{-1}` 不显式构造为矩阵，只保存向量 `m_inv[i] = 1 / A[i,i]`
3. 收敛判断使用平方残差 `rr = r^T r`

## 4.2 收敛阈值约定

Host 侧命令行输入的 `tau` 统一解释为与 `rr` 同量纲的阈值，即直接比较：

```text
rr <= tau
```

如果后续需要支持“残差二范数阈值”，必须在 host 侧显式平方后再下发，不在 kernel 内做隐式转换。

## 4.3 breakdown 与输入保护

必须实现以下保护：

1. 若某行缺失对角元，host 直接报错退出
2. 若 `abs(A[i,i]) <= eps`，host 直接报错退出
3. 若 `abs(p^T A p) <= eps`，host 打印 breakdown 并终止迭代
4. 若 `alpha`、`beta`、`rr`、`rz` 出现 `NaN/Inf`，host 终止并判失败

## 4.4 数据类型决策

第一版正式方案采用：

```cpp
using data_t = double;
using index_t = int;
```

原因：

1. 当前 `Project-XS` 的 golden、数据加载与现有 `Project-X` demo 都以 `double` 为主
2. 第一版重点是与 golden 精确对齐，而不是极限资源压缩
3. `n512` 规模较小，FP64 的资源压力在第一版可接受

为了保留后续切换空间，所有 kernel 与 host 公共头文件必须通过类型别名统一数据类型，后续可切换到 `float` 形成派生版本。

---

## 5. 总体架构决策

## 5.1 正式选择：Host 控制的多 kernel 管线

第一版正式架构定义为：

```text
Host/XRT 负责数据加载、m_inv 构造、迭代控制、收敛判断
Kernel 负责 SpMV、向量更新、点积等重计算步骤
```

整体流程如下：

```text
dataset loader
    ->
build m_inv
    ->
spmv_csr_kernel(x0 -> ax)
    ->
init_pcg_kernel(ax,b,m_inv -> r,z,p,rz,rr)
    ->
loop:
    spmv_csr_kernel(p -> ap)
    dot_kernel(p, ap -> pAp)
    host computes alpha
    update_xrz_kernel(alpha -> x,r,z,rz_new,rr)
    host computes beta
    update_p_kernel(beta -> p)
    if rr <= tau: stop
    ->
read back x
    ->
golden verification
```

这是一个明确的多 kernel 方案，同时避免了把强依赖迭代逻辑硬塞进多个相互直接耦合的 kernel 里。

## 5.2 不选择单顶层 solver kernel 的原因

仓库中已有一份文档建议“第一版可先做单顶层 kernel”。这个建议对于实现难度是合理的，但它不满足本任务的正式目标，因为这里要求的是 **多 kernel 版本**。  
因此本设计保留 host 控制迭代，把 kernel 划分成清晰的计算阶段，以满足：

1. 任务要求中的多 kernel 约束
2. 后续将 `SpMV` 或 `Dot` 单独升级/替换的可维护性
3. `hw_emu` 下更直接的阶段级调试能力

## 5.3 工程落地点

正式建议新增独立目录：

```text
Project-X/Project-XS/hls/cgsolver_jacobi_pcg/
├── README.md
├── include/
│   └── cg_common.hpp
├── kernels/
│   └── cg_kernels.cpp
├── host/
│   ├── host.cpp
│   └── dataset_loader.hpp
├── scripts/
│   ├── run_sw_emu.sh
│   ├── run_hw_emu.sh
│   └── run_hw.sh
├── connectivity_u55c.cfg
└── xrt.ini
```

这样做的原因是：

1. 不破坏当前根目录 `krnl_spmv` demo 的接口与构建逻辑
2. `Project-XS` 已经包含 solver 数据、golden 和 loader，语义上更贴近该问题
3. 多 kernel solver 需要新的 host 参数和新的 xclbin，不适合强塞进现有单 kernel demo 入口

---

## 6. Kernel 划分与接口

第一版正式采用 5 个 kernel。

## 6.1 `spmv_csr_kernel`

功能：

```text
y = A x
```

接口：

```cpp
extern "C" void spmv_csr_kernel(
    const index_t* row_ptr,
    const index_t* col_idx,
    const data_t* values,
    const data_t* x,
    data_t* y,
    int n
);
```

职责：

1. 初始化阶段计算 `ax = A x0`
2. 迭代阶段计算 `ap = A p`

实现要求：

1. 外层按 row 遍历
2. 内层遍历 `row_ptr[i] ~ row_ptr[i + 1]`
3. 将输入向量 `x` 缓存到片上本地数组，减少随机读
4. 不使用 `std::vector`、动态内存、文件 IO

## 6.2 `init_pcg_kernel`

功能：

```text
r  = b - ax
z  = M^{-1} r
p  = z
rz = r^T z
rr = r^T r
```

接口：

```cpp
extern "C" void init_pcg_kernel(
    const data_t* b,
    const data_t* ax,
    const data_t* m_inv,
    data_t* r,
    data_t* z,
    data_t* p,
    data_t* metrics,
    int n
);
```

输出约定：

```text
metrics[0] = rz
metrics[1] = rr
```

设计原因：

1. 初始化阶段的 `r/z/p/rz/rr` 彼此耦合，合成一个 kernel 可以减少额外 kernel 启动和中间标量往返
2. 对 `n512`，一次遍历中同时完成 `r/z/p` 更新和两个 reduction 是合理的

## 6.3 `dot_kernel`

功能：

```text
out[0] = a^T b
```

接口：

```cpp
extern "C" void dot_kernel(
    const data_t* a,
    const data_t* b,
    data_t* out,
    int n
);
```

当前仅用于：

```text
pAp = p^T ap
```

说明：

1. `rz` 与 `rr` 已在 `init_pcg_kernel` / `update_xrz_kernel` 中完成
2. 单独保留 `dot_kernel` 是为了让迭代中的 breakdown 检查有清晰边界
3. 后续如要扩展到更大规模，可再演进为 partial-sum + 二级 reduction

## 6.4 `update_xrz_kernel`

功能：

```text
x      = x + alpha p
r      = r - alpha ap
z      = M^{-1} r
rz_new = r^T z
rr     = r^T r
```

接口：

```cpp
extern "C" void update_xrz_kernel(
    data_t* x,
    const data_t* p,
    data_t* r,
    const data_t* ap,
    const data_t* m_inv,
    data_t* z,
    data_t* metrics,
    data_t alpha,
    int n
);
```

输出约定：

```text
metrics[0] = rz_new
metrics[1] = rr
```

设计原因：

1. `x/r/z` 更新天然共享同一轮 `alpha`
2. `rz_new` 与 `rr` 都依赖更新后的 `r`
3. 融合后可减少中间向量落地次数和 kernel 调度次数

## 6.5 `update_p_kernel`

功能：

```text
p = z + beta p
```

接口：

```cpp
extern "C" void update_p_kernel(
    const data_t* z,
    data_t* p,
    data_t beta,
    int n
);
```

说明：

1. `beta = rz_new / rz` 由 host 计算
2. 该 kernel 只承担方向向量更新，不混入额外 reduction

---

## 7. Host 侧正式职责

Host 不是“辅助脚本”，而是本方案中负责控制 Jacobi-PCG 迭代的正式执行层。

## 7.1 输入加载

Host 必须加载：

```text
row_ptr.txt
col_idx.txt
values.txt
b.txt
x0.txt
```

推荐直接复用或轻量封装：

```text
Project-X/Project-XS/src/CsrDataset.hpp
```

这样可减少重复 loader 带来的格式偏差。

## 7.2 Jacobi 预条件器构造

当前数据集没有 `m_inv.txt`，因此 host 必须：

```text
for each row i:
    find diag = A[i, i]
    validate |diag| > eps
    m_inv[i] = 1.0 / diag
```

这里建议直接复用 `extract_jacobi_diag()` 的逻辑，再在 host 中转成倒数向量。

## 7.3 设备 buffer 分配

Host 需要分配以下 BO：

```text
row_ptr
col_idx
values
b
x
r
z
p
spmv_out
m_inv
metrics
dot_out
```

其中：

1. `spmv_out` 在初始化阶段表示 `ax`，在迭代阶段表示 `ap`
2. `metrics` 复用于 `init_pcg_kernel` 和 `update_xrz_kernel`
3. 每轮迭代只回读 `dot_out[0]` 和 `metrics[0:1]`

## 7.4 迭代控制流程

Host 侧正式伪代码如下：

```cpp
dataset = load_dataset(data_dir);
m_inv = build_inverse_diag(dataset);
x = x0;

copy CSR / b / x / m_inv to device;

spmv_csr_kernel(row_ptr, col_idx, values, x, spmv_out, n);
init_pcg_kernel(b, spmv_out, m_inv, r, z, p, metrics, n);
read metrics -> rz, rr;

for (iter = 0; iter < Nmax && rr > tau; ++iter) {
    spmv_csr_kernel(row_ptr, col_idx, values, p, spmv_out, n);
    dot_kernel(p, spmv_out, dot_out, n);
    read dot_out[0] -> pAp;

    if (abs(pAp) <= eps) {
        status = breakdown;
        break;
    }

    alpha = rz / pAp;

    update_xrz_kernel(x, p, r, spmv_out, m_inv, z, metrics, alpha, n);
    read metrics -> rz_new, rr;

    beta = rz_new / rz;
    update_p_kernel(z, p, beta, n);

    rz = rz_new;
    print iter log;
}

copy x back to host;
verify against golden;
```

正式要求：

1. **不要** 每轮把完整 `x/r/p/z/ap` 读回 host
2. 每轮只允许回读必要标量
3. 最终再把 `x` 整体读回

---

## 8. HBM/BO 映射方案

第一版建议使用显式 bank 绑定，但前提是不能牺牲可编译性。

建议映射：

```text
row_ptr   -> HBM[0]
col_idx   -> HBM[0]
values    -> HBM[1]
b         -> HBM[2]
x         -> HBM[3]
r         -> HBM[4]
z         -> HBM[5]
p         -> HBM[6]
spmv_out  -> HBM[7]
m_inv     -> HBM[8]
metrics   -> HBM[9]
dot_out   -> HBM[9]
```

说明：

1. `row_ptr` 与 `col_idx` 读模式相近，可先共用 bank
2. `metrics` 与 `dot_out` 规模极小，共用 bank 无实质压力
3. 该映射主要是为后续扩大规模留结构，不是为了 `n512` 的即时性能

若某版工具链对多 kernel group id / bank 约束更敏感，允许第一版退回为“可编译优先”的较保守映射，但文档中的逻辑分区不变。

---

## 9. HLS 编码要求

所有 kernel 必须满足以下约束：

1. 顶层函数使用 `extern "C"`
2. 指针参数使用 `m_axi`
3. 标量参数使用 `s_axilite`
4. 不使用 STL 容器、异常、动态内存、文件 IO
5. 向量循环与 reduction 循环按需要加入 `#pragma HLS PIPELINE`
6. 使用公共头文件统一 `data_t / index_t / MAX_N`

## 9.1 本地缓存策略

第一版建议定义：

```cpp
static constexpr int MAX_N = 1024;
```

原因：

1. 当前仓库已有 `n64 / n512 / n1024` 三组数据
2. `MAX_N=1024` 对本地向量缓存仍然可控
3. v1 验证以 `n512` 为主，但不把实现硬编码死在 512

`spmv_csr_kernel` 中建议对输入向量做本地缓存：

```cpp
data_t x_local[MAX_N];
```

Host 必须在运行前检查：

```text
n <= MAX_N
```

若超限直接报错，不允许 silent truncation。

---

## 10. 验证方案与通过标准

## 10.1 参考基线

正式 golden 基线为：

```text
Project-X/Project-XS/src/CgSolverGolden.hpp
```

不允许在 HLS 工程里私自再写一份行为不同的“参考实现”。

## 10.2 必做检查

每次运行必须输出至少以下信息：

```text
[init] n=... nnz=... tau=... max_iters=... dtype=double
[iter 000] alpha=... beta=... rz=... rr=... residual=...
[iter 001] alpha=... beta=... rz=... rr=... residual=...
...
[done] iter=... residual_abs=... residual_rel=... status=converged|max_iter|breakdown
[check] cpu_residual_abs=... fpga_residual_abs=... max_abs_diff=...
```

## 10.3 验收标准

第一版建议验收条件：

1. `sw_emu` 与 CPU golden 在 `n512` 上结果一致收敛
2. `hw_emu` 能完整跑通
3. `residual_abs^2 <= tau` 或 `residual_rel <= 1e-10`（FP64 目标）
4. `x_fpga` 与 `x_golden` 的 `max_abs_diff` 保持在可接受范围内

对 `double` 第一版，期望差异应明显小于 `float` 版本。

---

## 11. 实现阶段划分

正式实施顺序定义如下：

### 阶段 0：复用现有 golden

1. 直接使用 `Project-XS` 的 `CsrDataset` 和 `CgSolverGolden`
2. 确认 `n512` 数据集可稳定复现收敛

### 阶段 1：完成多 kernel v1

1. 建立独立 HLS/XRT 子工程
2. 实现 5 个 kernel
3. 实现 host orchestration
4. 通过 `sw_emu`
5. 通过 `hw_emu`

### 阶段 2：补齐板卡映射与脚本

1. 完善 `connectivity_u55c.cfg`
2. 增加 `run_sw_emu.sh / run_hw_emu.sh / run_hw.sh`
3. 补充 README 与日志样例

### 阶段 3：可选优化

1. 多 CU `spmv_csr_chunk_kernel`
2. `float` 派生版本
3. 更激进的 reduction 优化
4. 根目录构建系统集成

---

## 12. 结论

本正式方案做出的关键决策是：

1. **算法上** 严格保持 Jacobi-PCG，不退化为普通 CG
2. **架构上** 采用 host 控制的 5-kernel 多阶段方案
3. **工程上** 作为 `Project-XS` 下的独立 HLS 子工程落地
4. **数值上** 第一版统一采用 `double`，与现有 golden 对齐
5. **验证上** 以 `n512` 数据集、`sw_emu/hw_emu` 和 CPU golden 对照为准

这套方案的目标不是一步做到最优性能，而是先把“可运行、可验证、可继续优化”的 Jacobi-PCG HLS 基线稳定建立起来。
