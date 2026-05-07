# Project-X：HLS 与 Chisel 的取舍说明

这份文档回答的问题是：

```text
这种自上而下的 HLS 写法，和传统 RTL 完全不一样。
如果目标是追求更高性能、尽量把带宽吃满，
是不是应该把 Chisel 穿插进来？
```

这份文档虽然放在 `Project-X` 目录里，但不会只谈这个小 demo。  
它会顺手把 `Project-X` 和更复杂的 `cgSolver` 对比起来，因为真正会把这个问题逼出来的，往往不是小型 SpMV，而是 `cgSolver` 这种多 kernel、HBM、多轮迭代系统。

---

## 1. 先给结论

一句话：

```text
可以引入 Chisel，但更适合作为“局部下沉到 RTL 级”的工具，
不适合作为第一步就替代整套 HLS 设计。
```

再具体一点：

- 对 `Project-X` 这种教学型 SpMV demo：HLS 已经很合适
- 对 `cgSolver` 这种真实多 kernel / FP64 / HBM / 迭代状态系统：Chisel 确实值得引入
- 但引入方式应该是：
  - 先保留 HLS 主线
  - 再把最带宽敏感、最需要微结构控制的局部模块下沉到 Chisel/RTL

所以不是：

```text
HLS 不行 -> 全部改 Chisel
```

而是：

```text
HLS 先把系统和算法跑通
Chisel 再接手 HLS 难以稳定逼近的关键微结构部分
```

---

## 2. 为什么会觉得 HLS 和 RTL “完全不一样”

这是正常感觉，因为它们关注的层次本来就不同。

### 2.1 HLS 的视角

在 HLS 里，你写的是：

- 算法循环
- 数组访问
- pragma
- 顶层接口

比如 `Project-X` 里的：

```cpp
RowLoop:
for (int row = 0; row < num_rows; ++row) {
#pragma HLS PIPELINE II=1
    ...
}
```

你看到的是“按行做 SpMV”。

### 2.2 RTL / Chisel 的视角

在 RTL 或 Chisel 里，你更容易直接关心：

- 读请求什么时候发
- outstanding transaction 有几个
- 写回和读出是否解耦
- FIFO 深度多大
- reduction tree 怎么布
- 哪一级寄存器切断关键路径
- 一个 bank 是否会被多个访存源争抢

你看到的不是“算法大意”，而是“硬件微结构”。

所以两者不是谁高级谁低级，而是：

```text
HLS 更像描述“想做什么”
RTL/Chisel 更像描述“硬件每一级怎么做”
```

---

## 3. `Project-X` 为什么更适合 HLS

`Project-X` 这个 demo 的特点是：

- 只有一个 kernel
- 算法很清楚：SpMV
- 输入输出简单
- host 生成数据
- HBM bank 只分了 4 路
- 目标是学习 Vitis/XRT/HLS 这条链路

这里 HLS 的优势很大：

1. **代码短**
   算法和接口能放在一个文件里讲清楚。

2. **教学闭环完整**
   你能直接从：
   - `krnl_spmv.cpp`
   - `PIPELINE/UNROLL`
   - `csynth.rpt`
   - 生成的 `krnl_spmv.v`
   - `host.cpp`
   - `xclbin`
   这条链顺下来。

3. **不需要一开始就处理复杂内存引擎**
   这个 demo 的重点是理解概念，而不是把每个 AXI master 压榨到极限。

所以对 `Project-X` 来说，HLS 是一个非常合适的入口。

---

## 4. 为什么 `cgSolver` 更容易把 Chisel 的价值逼出来

`cgSolver` 和 `Project-X` 最大的区别不是“规模更大”，而是问题性质完全不同。

### 4.1 它不是单 kernel

`cgSolver` 里有：

- sparse SpMV 主链
- `storeApk`
- `update_xk`
- `update_pk`
- `update_rk_jacobi`
- `control / duplicate / timer`

这已经不是一个“函数综合成一个 kernel”的问题，而是一个多 compute unit 系统。

### 4.2 它不是一次性算完

SpMV 出错，通常是某些输出值错。  
PCG 出错，可能是：

- iter 0 看起来没事
- iter 1 开始 `alpha/beta/rz` 发散
- 后面整个求解崩掉

所以 `cgSolver` 更怕：

- 同 BO in/out 读写顺序
- token 时序
- reduction 顺序
- buffer 回写时机

这些都更接近 RTL 级问题。

### 4.3 它高度依赖访存和 reduction 微结构

`cgSolver` 里真正决定性能和稳定性的，不只是公式，而是：

- HBM bank 分配
- AXI master 数量
- burst 形成能力
- outstanding request
- FP64 dot / norm / axpy 的流水线结构
- 读旧值 / 写新值是否隔离

这些地方正是 Chisel/RTL 更容易精确控制的。

---

## 5. “吃满带宽”到底靠什么

先说一个容易误会的点：

```text
换成 Chisel，不会自动让带宽跑满。
```

带宽能否吃满，通常取决于：

- 访问是否规整
- burst 是否连续
- bank 是否冲突
- outstanding request 是否足够
- 计算流水线是否跟得上数据
- 是否存在读写互相背压

Chisel能提供的真正价值是：

```text
让这些结构从“交给 HLS 猜”变成“我自己明确设计”
```

比如你可以更明确地做：

- 自己的 AXI read engine
- 自己的 AXI write engine
- 明确的双缓冲 / ping-pong
- 固定 reduction tree
- token/data decoupling
- bank-aware arbitration

---

## 6. 哪些地方最适合先用 Chisel

如果目标是逐步把 `cgSolver` 的关键路径做得更可控，我最推荐的顺序是：

### 6.1 第一层：状态更新 kernel

优先级最高：

- `update_xk`
- `update_pk`
- `update_rk_jacobi`
- `storeApk`

原因：

- 数据更规整
- 更像向量流处理
- 更容易做 deterministic RTL
- 和当前板上 iter1 后发散的问题直接相关

这里 Chisel 特别适合做：

- vec4 packed 数据通路
- 读旧值 / 写新值分离
- 固定长度 burst
- lane 级 FP64 pipeline
- 可复用 token codec

### 6.2 第二层：memory shell / 通用组件

适合做成 Chisel 组件库的东西：

- `AxiReadMasterVec4`
- `AxiWriteMasterVec4`
- `Fp64Vec4Axpy`
- `Fp64Vec4Dot`
- `TokenCodec`

这层不是替某个 kernel，而是在搭以后的 RTL 基座。

### 6.3 第三层：稀疏 SpMV 主链

最后才考虑：

- `selMultXkernel`
- `rowAccKernel`
- `assembleYkernel`

因为这里的不规则 `x[col]` 访问本身就很难，不是单靠“改成 Chisel”就能 magically 变成满带宽。

所以：

```text
Chisel 对规整状态向量路径的收益，通常比对稀疏 gather 主链更直接。
```

---

## 7. 什么情况下继续用 HLS 更划算

如果当前目标是下面这些，优先继续用 HLS：

- 先把系统跑通
- 先定位数值 bug
- 快速改一条 pragma / bank / bundle
- 对比 C-sim / kernel-step / sw_emu / hw
- 保持和现有 Vitis/HLS 链路兼容

理由是：

- HLS 迭代更快
- 现有 `cgSolver` 主体已经是 HLS
- 当前测试脚本、日志和验证链都围绕 HLS 建起来了

也就是说：

```text
在“系统级调通”阶段，HLS 的效率通常更高；
在“微结构级优化”阶段，Chisel 的价值才开始拉开。
```

---

## 8. 一个现实可行的路线

如果目标是：

```text
短期把 cgSolver 板上结果稳定下来
中期再追更高性能和更强可控性
```

那我建议路线是：

1. **短期**
   继续以 HLS 版本为主线，把：
   - 数值收敛
   - host/XRT 链路
   - HBM bank / bundle
   - bitstream 构建
   这些先稳定。

2. **中期**
   用 Chisel 做一个最小 RTL kernel 试点，优先从：
   - `update_xk`
   或
   - `update_pk`
   开始。

3. **确认试点打通**
   证明它能：
   - 打成 `.xo`
   - 接进 Vitis
   - 通过 U55C link
   - 板上能被现有 host 调起来

4. **再逐步扩展**
   往 `storeApk` / `update_rk_jacobi` 推进。

5. **最后再碰 sparse SpMV 主链**
   除非已经明确决定长期维护一套 Chisel sparse accelerator。

---

## 9. 最后一句结论

如果只回答最核心的问题：

```text
为了追求高性能、吃满带宽，是不是 Chisel 穿插进来更合适？
```

我的答案是：

```text
对 cgSolver 这种复杂系统，是的，Chisel 值得穿插进来；
但最佳插入点不是一上来替掉整套 HLS，而是先下沉到状态更新层和 memory shell 层。
```

更直白一点：

- `Project-X`：HLS 足够好，适合教学和原型
- `cgSolver`：当你开始追求“更稳的时序、更可控的访存、逼近满带宽”时，Chisel 开始真正有价值

---

## 10. 推荐搭配阅读

如果你想把这个判断和工程细节串起来，建议顺着看：

- `~/ProjectFS/Project-X/docs/C层到硬件实现_zh.md`
- `~/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md`
- `~/ProjectFS/Project-X/docs/BO与HBM映射_zh.md`
- `~/ProjectFS/test/hpc/pcg/Chisel_reimplementation_notes_zh.md`

