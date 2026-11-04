# One guarded implementation path; preserve original synthesis/physical runs.
# This performs diagnostic place/route and refuses unqualified release results.
set ::env(VIDEO_DMA_ROUTE) 1
set guarded_build [file join [file dirname [file normalize [info script]]] build_video_dma.tcl]
source $guarded_build
