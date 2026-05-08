package projectx

import chisel3._

// 这个 BlackBox 只描述 Vivado `floating_point` IP 的模块名和端口，
// 真正的 RTL 由 OuterProductTileFiles.scala 生成的
// create_outer_product_tile_dmul_ip.tcl 在构建前落盘。
//
// 端口语义如下：
// - aclk: FP64 multiply IP 的工作时钟，来自外层 wrapper 的 ap_clk
// - aclken: 时钟使能，来自 ap_ce；HLS stall 时可以冻结内部流水
// - s_axis_a_tvalid / s_axis_a_tdata: A 操作数握手和数据，数据是 IEEE-754 double 的 bit pattern
// - s_axis_b_tvalid / s_axis_b_tdata: B 操作数握手和数据，数据是 IEEE-754 double 的 bit pattern
// - m_axis_result_tvalid: 结果有效，固定 latency 后拉高
// - m_axis_result_tdata: 乘法结果，同样按 IEEE-754 double bit pattern 输出
class OuterProductTileDmulIp extends BlackBox {
  override def desiredName: String = "outer_product_tile_dmul_ip"

  val io = IO(new Bundle {
    val aclk = Input(Clock())
    val aclken = Input(Bool())
    val s_axis_a_tvalid = Input(Bool())
    val s_axis_a_tdata = Input(UInt(64.W))
    val s_axis_b_tvalid = Input(Bool())
    val s_axis_b_tdata = Input(UInt(64.W))
    val m_axis_result_tvalid = Output(Bool())
    val m_axis_result_tdata = Output(UInt(64.W))
  })
}

// 8x8 外积 tile 的 HLS black-box wrapper。
//
// 当前工程里真正被 kernel 调用的是这个 tile 版本，而不是旧的单元素版本：
// - hardware/chisel_core/krnl_spmv.cpp
// - hardware/hybrid/krnl_spmv.cpp
//
// 这两个 kernel 都会 include 生成出来的 outer_product_tile.hpp，然后用：
// 1. PROJECTX_OUTER_PRODUCT_TILE_DECLARE_OUTPUTS(tile_out)
// 2. PROJECTX_OUTER_PRODUCT_TILE_CALL(lhs_tile, rhs_tile, tile_out)
// 3. PROJECTX_OUTER_PRODUCT_TILE_COPY_OUTPUTS(tile_out, out_tile)
//
// 宏展开后，HLS 看到的是一个 C 函数调用：
//   outer_product_tile_bits(lhs0_bits, ..., lhs7_bits,
//                           rhs0_bits, ..., rhs7_bits,
//                           out0_bits, ..., out63_bits)
//
// Chisel wrapper 内部并行实例化 64 个 FP64 multiply IP，同一拍发起整块 tile
// 的乘法，固定 latency 后一次性给出 64 个结果。这样 HLS 不再需要对外积矩阵
// 的每个元素单独调用一次 ap_ctrl_chain black-box。
//
// 数据端口约定：
// - lhs0_bits .. lhs7_bits: tile 的 8 个“行元素”，决定输出的 row 维
// - rhs0_bits .. rhs7_bits: tile 的 8 个“列元素”，决定输出的 col 维
// - out(row * 8 + col)_bits: row-major 展平后的结果，值等于 lhs(row) * rhs(col)
// - outN_bits_ap_vld: HLS 需要的逐端口 valid 信号；虽然名字是每个端口一个，
//   但本实现让 64 个 valid 同拍拉高，因为 64 路乘法结果总是一起返回
//
// 控制端口约定：
// - ap_start 拉高时，lhs/rhs 输入必须稳定；wrapper 会在同一拍把 64 路乘法同时发起
// - ap_ce 为低时，内部 validPipe 和 FP IP 流水都被冻结
// - ap_done / outN_bits_ap_vld 在结果可被 HLS 消费的那个拍拉高
// - ap_continue 参与最终 done 判定，避免 HLS 尚未准备好时提前“交付结果”
class OuterProductTile extends RawModule {
  override def desiredName: String = "outer_product_tile_bits"

  // 固定做 8x8 外积 tile；如果以后要改 tile 大小，HLS 头文件宏、JSON 端口、
  // 调用点数组长度都要一起改。
  private val tileSize = 8
  private val outputCount = tileSize * tileSize
  // 必须和 outer_product_tile.json 的 latency 以及 create_outer_product_tile_dmul_ip.tcl
  // 的 CONFIG.c_latency 保持一致。
  private val fpLatency = 6

  // HLS ap_ctrl_chain 控制信号。名字直接被 outer_product_tile.json 引用。
  val ap_clk = IO(Input(Clock()))
  val ap_rst = IO(Input(Bool()))
  val ap_ce = IO(Input(Bool()))
  val ap_start = IO(Input(Bool()))
  val ap_continue = IO(Input(Bool()))

  val ap_idle = IO(Output(Bool()))
  val ap_done = IO(Output(Bool()))
  val ap_ready = IO(Output(Bool()))

  // 16 个标量输入端口，对应 HLS C 函数参数里的 lhs0..7 / rhs0..7。
  // 这里故意不做 Vec IO，而是展开成标量端口，方便 black-box JSON 一对一映射。
  private val lhsBits =
    Seq.tabulate(tileSize)(i => IO(Input(UInt(64.W))).suggestName(s"lhs${i}_bits"))
  private val rhsBits =
    Seq.tabulate(tileSize)(i => IO(Input(UInt(64.W))).suggestName(s"rhs${i}_bits"))
  // 64 个标量输出端口，按 row-major 顺序编码：
  // out(index) 其中 index = row * tileSize + col。
  private val outBits =
    Seq.tabulate(outputCount)(i => IO(Output(UInt(64.W))).suggestName(s"out${i}_bits"))
  // HLS JSON 给每个输出都声明了一个 valid 端口，所以这里也逐个生成同名端口。
  private val outValids =
    Seq.tabulate(outputCount)(i => IO(Output(Bool())).suggestName(s"out${i}_bits_ap_vld"))

  // 把 wrapper 统一的控制/数据协议接到单个乘法 IP 上。
  private def connectMul(ip: OuterProductTileDmulIp, lhs: UInt, rhs: UInt, valid: Bool): Unit = {
    ip.io.aclk := ap_clk
    ip.io.aclken := ap_ce
    ip.io.s_axis_a_tvalid := valid
    ip.io.s_axis_a_tdata := lhs
    ip.io.s_axis_b_tvalid := valid
    ip.io.s_axis_b_tdata := rhs
  }

  // 对每个输出坐标 (row, col) 放一颗独立的乘法 IP：
  //   mul(index) = lhsBits(row) * rhsBits(col)
  // 输出端口顺序与 HLS 头文件/JSON 的 out0_bits..out63_bits 完全一致。
  private val muls = Seq.tabulate(outputCount) { index =>
    val row = index / tileSize
    val col = index % tileSize
    val mul = Module(new OuterProductTileDmulIp)
    connectMul(mul, lhsBits(row), rhsBits(col), ap_start)
    outBits(index) := mul.io.m_axis_result_tdata
    mul
  }

  // 64 个结果必须在同一拍一起有效，tile 级输出才算完成。
  private val allResultsValid = muls.map(_.io.m_axis_result_tvalid).reduce(_ && _)

  withClockAndReset(ap_clk, ap_rst) {
    // 用固定长度 validPipe 跟踪“这次 ap_start 对应的 tile 结果应在哪一拍返回”。
    val validPipe = RegInit(VecInit(Seq.fill(fpLatency)(false.B)))

    when(ap_ce) {
      validPipe(0) := ap_start
      for (stage <- 1 until fpLatency) {
        validPipe(stage) := validPipe(stage - 1)
      }
    }

    // 只有当：
    // 1. 固定 latency 已经走完
    // 2. 64 个底层乘法 IP 都报告 result valid
    // 3. HLS 侧给出 ap_continue
    // 才把这一整块 tile 作为一次 black-box 调用的完成结果交付出去。
    val tileValid = validPipe(fpLatency - 1) && allResultsValid && ap_continue
    ap_done := tileValid
    for (index <- 0 until outputCount) {
      outValids(index) := tileValid
    }
  }

  // 当前 HLS 使用方式下，black-box 不在 pipeline loop 内，简单组合式 ready/idle
  // 就足够满足调度；如果将来把它放入更复杂的链式场景，这里的定义需要一起复核。
  ap_ready := ap_start
  ap_idle := !ap_start
}
