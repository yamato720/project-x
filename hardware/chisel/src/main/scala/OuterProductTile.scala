package projectx

import chisel3._

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

// 8x8 外积 tile black-box。
//
// HLS 侧按 tile 调一次：
//   outer_product_tile_bits(lhs_tile[8], rhs_tile[8], out_tile[64])
//
// Chisel wrapper 内部并行实例化 64 个 FP64 multiply IP，同一拍发起整块 tile
// 的乘法，固定 latency 后一次性给出 64 个结果。这样 HLS 不再需要对外积矩阵
// 的每个元素单独调用一次 ap_ctrl_chain black-box。
class OuterProductTile extends RawModule {
  override def desiredName: String = "outer_product_tile_bits"

  private val tileSize = 8
  private val outputCount = tileSize * tileSize
  private val fpLatency = 6

  val ap_clk = IO(Input(Clock()))
  val ap_rst = IO(Input(Bool()))
  val ap_ce = IO(Input(Bool()))
  val ap_start = IO(Input(Bool()))
  val ap_continue = IO(Input(Bool()))

  val ap_idle = IO(Output(Bool()))
  val ap_done = IO(Output(Bool()))
  val ap_ready = IO(Output(Bool()))

  private val lhsBits =
    Seq.tabulate(tileSize)(i => IO(Input(UInt(64.W))).suggestName(s"lhs${i}_bits"))
  private val rhsBits =
    Seq.tabulate(tileSize)(i => IO(Input(UInt(64.W))).suggestName(s"rhs${i}_bits"))
  private val outBits =
    Seq.tabulate(outputCount)(i => IO(Output(UInt(64.W))).suggestName(s"out${i}_bits"))
  private val outValids =
    Seq.tabulate(outputCount)(i => IO(Output(Bool())).suggestName(s"out${i}_bits_ap_vld"))

  private def connectMul(ip: OuterProductTileDmulIp, lhs: UInt, rhs: UInt, valid: Bool): Unit = {
    ip.io.aclk := ap_clk
    ip.io.aclken := ap_ce
    ip.io.s_axis_a_tvalid := valid
    ip.io.s_axis_a_tdata := lhs
    ip.io.s_axis_b_tvalid := valid
    ip.io.s_axis_b_tdata := rhs
  }

  private val muls = Seq.tabulate(outputCount) { index =>
    val row = index / tileSize
    val col = index % tileSize
    val mul = Module(new OuterProductTileDmulIp)
    connectMul(mul, lhsBits(row), rhsBits(col), ap_start)
    outBits(index) := mul.io.m_axis_result_tdata
    mul
  }

  private val allResultsValid = muls.map(_.io.m_axis_result_tvalid).reduce(_ && _)

  withClockAndReset(ap_clk, ap_rst) {
    val validPipe = RegInit(VecInit(Seq.fill(fpLatency)(false.B)))

    when(ap_ce) {
      validPipe(0) := ap_start
      for (stage <- 1 until fpLatency) {
        validPipe(stage) := validPipe(stage - 1)
      }
    }

    val tileValid = validPipe(fpLatency - 1) && allResultsValid && ap_continue
    ap_done := tileValid
    for (index <- 0 until outputCount) {
      outValids(index) := tileValid
    }
  }

  ap_ready := ap_start
  ap_idle := !ap_start
}
