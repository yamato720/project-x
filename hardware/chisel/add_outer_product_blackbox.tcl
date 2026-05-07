set project_root [file normalize [file join [file dirname [info script]] ".." ".."]]
# 这个 Tcl 会通过 Makefile 的 --hls.pre_tcl 传给 v++。
# Vitis HLS 默认只会加入 Makefile 按 VARIANT 选中的 krnl_spmv_*.cpp；
# 外部 RTL black-box 的 JSON 不会被自动发现，所以必须在 csynth_design 前
# 显式 add_files -blackbox。
#
# JSON 里描述了三件关键事情：
# 1. C 函数 outer_product_tile_bits 对应哪个 RTL 顶层模块
# 2. C 参数如何映射到 RTL 数据端口和 ap_ctrl_chain 控制端口
# 3. Chisel wrapper、Vivado IP wrapper、floating_point IP RTL 文件在哪里
add_files -blackbox [file join $project_root "hardware/chisel/generated/outer/outer_product_tile.json"]
