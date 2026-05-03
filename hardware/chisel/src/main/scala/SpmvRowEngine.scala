package projectx

import chisel3._
import chisel3.util.{log2Ceil, Valid}

class SpmvRowEngine(
  val slotsPerRow: Int = 3,
  val dataWidth: Int = 32,
  val indexWidth: Int = 16
) extends Module {
  require(slotsPerRow > 0, "slotsPerRow must be positive")
  require(dataWidth > 0, "dataWidth must be positive")
  require(indexWidth > 0, "indexWidth must be positive")

  val laneProductWidth: Int = dataWidth * 2
  val rowSumWidth: Int = laneProductWidth + log2Ceil(slotsPerRow) + dataWidth

  val io = IO(new Bundle {
    val start = Input(Bool())
    val scale = Input(SInt(dataWidth.W))
    val slotValid = Input(Vec(slotsPerRow, Bool()))
    val columnIndex = Input(Vec(slotsPerRow, SInt(indexWidth.W)))
    val values = Input(Vec(slotsPerRow, SInt(dataWidth.W)))
    val xValues = Input(Vec(slotsPerRow, SInt(dataWidth.W)))

    val rowValid = Output(Bool())
    val rowSum = Output(SInt(rowSumWidth.W))
  })

  val laneProducts = Wire(Vec(slotsPerRow, Valid(SInt(laneProductWidth.W))))

  for (lane <- 0 until slotsPerRow) {
    val laneIsActive = io.start && io.slotValid(lane) && (io.columnIndex(lane) >= 0.S)
    laneProducts(lane).valid := laneIsActive
    laneProducts(lane).bits := io.values(lane) * io.xValues(lane)
  }

  val merge = Module(new MergeReducer(slotsPerRow, laneProductWidth))
  merge.io.in := laneProducts

  val scaledResult = merge.io.out.bits * io.scale
  val rowResultReg = RegInit(0.S.asTypeOf(chiselTypeOf(scaledResult)))

  when(merge.io.out.valid) {
    rowResultReg := scaledResult
  }

  io.rowValid := RegNext(merge.io.out.valid, false.B)
  io.rowSum := rowResultReg
}
