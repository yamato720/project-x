package projectx

import chisel3.stage.ChiselStage

object GenerateMergeReducer extends App {
  (new ChiselStage).emitVerilog(
    new MergeReducer(lanes = 3, dataWidth = 64),
    Array("--target-dir", "generated/merge")
  )
}

object GenerateSpmvRowEngine extends App {
  (new ChiselStage).emitVerilog(
    new SpmvRowEngine(slotsPerRow = 3, dataWidth = 32, indexWidth = 16),
    Array("--target-dir", "generated/spmv")
  )
}

object GenerateAll extends App {
  GenerateMergeReducer.main(Array.empty)
  GenerateSpmvRowEngine.main(Array.empty)
}
