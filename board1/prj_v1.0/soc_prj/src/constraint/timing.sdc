# MIPI uses rx_clk_gen / DR1_PHY_LCLK DIV=2 and DIV=4, not the old MIPIIO.
# Retain the original maximum byte-clock budget of 156.25 MHz, corresponding
# to a forwarded DDR clock of 625 MHz. This is an upper-bound constraint,
# not a measurement. Revalidate source-synchronous data delays for each mode.
create_clock -name mipi_hs_clock -period 1.6 -waveform {0 0.8} [get_ports {I_clk_hs_p}]

# PS FCLK=50 MHz; one existing PLL supplies GP0=75 and core/bus=150 MHz.
# Native derivation preserves the PS interface library timing model.
# Independent camera/HDMI PLLs retain their own derived outputs.
derive_clocks

# No empty clock groups or blanket AXI/camera false paths.
# CDC exceptions require reviewed, matching synchronizer/FIFO endpoints.
