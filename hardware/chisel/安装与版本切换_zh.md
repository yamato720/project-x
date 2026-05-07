# Chisel 安装与版本切换

本文说明 `Project-X/hardware/chisel` 当前使用的 Chisel 工具链、安装方式，以及如何在多版本 OpenJDK 环境下切换。

## 当前版本

当前工程已经从旧的 Chisel3 `3.6.0` 升级到：

```text
Chisel        7.11.0
Scala         2.13.18
sbt           1.11.7
firtool       由 Chisel 自动管理，当前 Chisel 7.11.0 对应 firtool 1.144.0
推荐 JDK      OpenJDK 17+
当前服务器    OpenJDK 17.0.18
```

对应配置文件：

```text
hardware/chisel/build.sbt
hardware/chisel/project/build.properties
```

当前 `build.sbt` 使用新的 artifact：

```scala
"org.chipsalliance" %% "chisel" % "7.11.0"
"org.chipsalliance" % "chisel-plugin" % "7.11.0"
```

不再使用旧的：

```scala
"edu.berkeley.cs" %% "chisel3" % "3.6.0"
"edu.berkeley.cs" % "chisel3-plugin" % "3.6.0"
```

## 官方版本依据

Chisel 官方安装文档说明：

- Chisel 是 Scala library + compiler plugin。
- Chisel 7.x 对应 Scala `2.13.18`。
- Chisel 推荐使用 LTS JDK 17 或更新版本。
- Chisel 6.0 之后通常会自动管理 firtool。

参考：

```text
https://www.chisel-lang.org/docs/installation
```

官方 latest Chisel 示例使用：

```text
Scala 2.13.18
org.chipsalliance::chisel:7.11.0
org.chipsalliance:::chisel-plugin:7.11.0
```

参考：

```text
https://github.com/chipsalliance/chisel/releases/latest/download/chisel-example.scala
```

## 服务器当前 JDK

查看当前 Java：

```bash
java -version
javac -version
which java
which javac
```

当前服务器可用：

```text
/usr/lib/jvm/java-11-openjdk-amd64
/usr/lib/jvm/java-17-openjdk-amd64
```

Chisel 7 建议用 OpenJDK 17，所以工程默认 Makefile 会优先选择：

```text
/usr/lib/jvm/java-17-openjdk-amd64
```

## 推荐生成方式

从项目根目录运行：

```bash
cd ~/ProjectFS/Project-X
make chisel
```

`make chisel` 会先检查 Java：

```text
CHISEL_JAVA_HOME/bin/java
CHISEL_JAVA_HOME/bin/javac
```

然后执行：

```bash
cd hardware/chisel
JAVA_HOME="$CHISEL_JAVA_HOME" PATH="$CHISEL_JAVA_HOME/bin:$PATH" sbt "runMain projectx.GenerateAll"
```

## 临时切换 OpenJDK

推荐方式是不改系统默认 Java，只给 Chisel 生成命令指定 `CHISEL_JAVA_HOME`。

使用 OpenJDK 17：

```bash
make chisel CHISEL_JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
```

如果只是想确认检查逻辑：

```bash
make chisel-env CHISEL_JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
```

不要用 Java 11 跑当前 Chisel 7 配置。虽然部分 JVM 版本可能还能启动，但项目现在按 Chisel 7.x / Scala 2.13.18 / JDK 17+ 管理，统一用 OpenJDK 17 更稳。

## Shell 中手动切换

如果你直接在 `hardware/chisel` 目录里跑 sbt，可以这样：

```bash
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"
cd ~/ProjectFS/Project-X/hardware/chisel
sbt "runMain projectx.GenerateAll"
```

检查 sbt 实际使用的 Java：

```bash
sbt --version
```

## 系统级切换

如果你确实要改系统默认 Java，可以使用 `update-alternatives`：

```bash
sudo update-alternatives --config java
sudo update-alternatives --config javac
```

不推荐为了单个工程频繁改系统默认值。优先使用：

```bash
make chisel CHISEL_JAVA_HOME=/path/to/jdk
```

这样不会影响 Vivado、Vitis、其他 Scala/Java 项目。

## Ubuntu 安装 OpenJDK 17

如果机器没有 OpenJDK 17：

```bash
sudo apt update
sudo apt install openjdk-17-jdk
```

安装后确认：

```bash
/usr/lib/jvm/java-17-openjdk-amd64/bin/java -version
/usr/lib/jvm/java-17-openjdk-amd64/bin/javac -version
```

## sbt 安装

工程使用 `project/build.properties` 固定 sbt launcher 版本：

```text
sbt.version=1.11.7
```

机器上只需要有 `sbt` 启动脚本。sbt 启动后会自动下载 `1.11.7` launcher。

Ubuntu 常见安装方式：

```bash
sudo apt install sbt
```

如果系统仓库没有 sbt，也可以按官方 sbt 文档安装启动脚本。

## 生成结果

执行：

```bash
make chisel
```

会生成：

```text
hardware/chisel/generated/merge/MergeReducer.v
hardware/chisel/generated/spmv/SpmvRowEngine.v
hardware/chisel/generated/outer/outer_product_mul_bits.v
hardware/chisel/generated/outer/outer_product_mul.hpp
hardware/chisel/generated/outer/outer_product_mul_model.cpp
hardware/chisel/generated/outer/outer_product_mul.json
hardware/chisel/generated/outer/create_outer_product_mul_dmul_ip.tcl
hardware/chisel/generated/outer/ip/outer_product_mul_dmul_ip/...
```

其中 `outer_product_mul_bits.v` 是 HLS black-box wrapper，内部实例化：

```text
outer_product_mul_dmul_ip
```

这个 IP 仍然由 Vivado Floating Point IP 生成，不由 Chisel 自己实现 FP64 乘法。

## 升级后代码变化

旧版 Chisel 3.6 常见写法：

```scala
import chisel3.stage.ChiselStage

(new ChiselStage).emitVerilog(...)
```

当前 Chisel 7 使用：

```scala
import _root_.circt.stage.ChiselStage

ChiselStage.emitSystemVerilog(...)
```

此外，Chisel 7 / firtool 对公开端口位宽更严格。旧代码中的：

```scala
Output(Valid(SInt()))
Output(SInt())
```

已经改成显式宽度：

```scala
Output(Valid(SInt(outputWidth.W)))
Output(SInt(rowSumWidth.W))
```

否则 firtool 会报：

```text
public module port must have known width
```

## 验证命令

最小验证：

```bash
make chisel
```

如果要继续验证 HLS 能吃到新生成的 black-box：

```bash
make xo TARGET=sw_emu
```

硬件路径已经比较重，除非确实要重新综合，否则不要直接跑：

```bash
make build TARGET=hw
```
