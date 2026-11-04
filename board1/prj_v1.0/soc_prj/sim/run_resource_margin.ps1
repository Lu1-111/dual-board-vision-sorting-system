param([string]$Iverilog='E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp='E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'run_fdma.ps1') -Iverilog $Iverilog -Vvp $Vvp
if($LASTEXITCODE -ne 0){throw 'FDMA regressions failed'}
$projectDir=Split-Path $PSScriptRoot -Parent
$rtl=Join-Path $projectDir 'src/write_fdma'
$buildDir=Join-Path $PSScriptRoot 'build-fdma'
$outputFile=Join-Path $buildDir 'fdma_service_stall.vvp'
# 156.25 MHz input, 150 MHz AXI, 480 four-pixel words/line, line period
# approximately 14.82 us. Finite 3000-core-cycle (20 us) DDR interruption.
& $Iverilog -g2012 -Wall -s fdma_write_tb '-Pfdma_write_tb.XSIZE=480' '-Pfdma_write_tb.YSIZE=40' '-Pfdma_write_tb.FIFO_DEPTH=4096' '-Pfdma_write_tb.PIXEL_HALF=3.2' '-Pfdma_write_tb.CORE_HALF=3.333333' '-Pfdma_write_tb.LINE_GAP=1834' '-Pfdma_write_tb.DDR_STALL_CYCLES=3000' -o $outputFile (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'uiFDMA.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'fdma_write_tb.sv')
if($LASTEXITCODE -ne 0){throw 'Service-stall FDMA compilation failed'}
& $Vvp $outputFile
if($LASTEXITCODE -ne 0){throw 'Service-stall FDMA simulation failed'}
$capacityOutput=Join-Path $buildDir 'capture_fifo_capacity.vvp'
& $Iverilog -g2012 -Wall -s capture_fifo_capacity_tb -o $capacityOutput (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'capture_fifo_capacity_tb.sv')
if($LASTEXITCODE -ne 0){throw 'Capacity FDMA compilation failed'}
& $Vvp $capacityOutput
if($LASTEXITCODE -ne 0){throw 'Capacity FDMA simulation failed'}
& python -m unittest discover -s $PSScriptRoot -p 'test_*.py'
if($LASTEXITCODE -ne 0){throw 'Constraint/release regression failed'}
