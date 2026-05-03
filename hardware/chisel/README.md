# Project-X Chisel

这个目录是 `Project-X/hardware/` 下的独立 Chisel 工程，当前使用 Chisel3 `3.6.0`。

## 当前模块

```text
hardware/chisel/
  build.sbt
  project/build.properties
  src/main/scala/MergeReducer.scala
  src/main/scala/SpmvRowEngine.scala
  src/main/scala/Generate.scala
```

- `MergeReducer`
  负责把多个 lane 的部分结果做归并求和。
- `SpmvRowEngine`
  负责单行 `SpMV` 的乘加骨架：每个 slot 做 `value * xValue`，再交给 `MergeReducer` 汇总，最后乘上 `scale`。

## 设计边界

当前 Chisel 版本用的是有符号整数 datapath，目的是先把结构搭清楚：

- `slotValid / columnIndex` 的行内控制
- 多 lane 部分积生成
- reduction tree 归并
- 行结果寄存

它还不是 FP64 版本。  
如果你要和现有 HLS `double` kernel 一一对齐，下一步应引入专门的浮点单元或 HardFloat/厂商 FP IP。

## 生成 Verilog

```bash
cd /home/pyx/ProjectFS/Project-X/hardware/chisel
sbt "runMain projectx.GenerateAll"
```

或者在项目根目录：

```bash
make chisel
```

输出默认写到：

```text
hardware/chisel/generated/merge/
hardware/chisel/generated/spmv/
```

## 关于“归并模块能不能求行列式”

不能直接这样理解。

- `SpMV` 给的是 `y = A * x`
- 行列式需要的是矩阵整体结构
- 归并模块只能做加和、合并、归约这一类局部数据路径工作

所以 `MergeReducer` 能做的是：

- 汇总 `SpMV` 的部分和
- 做 dot-product / reduction tree 的基础组件

它不能单靠 `SpMV` 输出就推出 `det(A)`。  
如果以后要做行列式或分解，更接近的是 `LU/QR/Cholesky` 这类模块链路。
