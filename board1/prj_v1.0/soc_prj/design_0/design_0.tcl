# Open the authoritative current system graph. The old partial export used
# obsolete D: paths and must not recreate a different clock/reset topology.
set design_dir [file dirname [file normalize [info script]]]
si_open_design $design_dir
