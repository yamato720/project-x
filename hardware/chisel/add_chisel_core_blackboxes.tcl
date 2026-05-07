set project_root [file normalize [file join [file dirname [info script]] ".." ".."]]
add_files -blackbox [file join $project_root "hardware/chisel/generated/outer/outer_product_tile.json"]
add_files -blackbox [file join $project_root "hardware/chisel/generated/spmv_row/spmv_row_muladd.json"]
