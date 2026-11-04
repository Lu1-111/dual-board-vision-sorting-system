param([string]$Iverilog='E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp='E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path $Iverilog -Parent)+';'+$env:PATH
$projectDir=Split-Path $PSScriptRoot -Parent
$buildDir=Join-Path $PSScriptRoot 'build-fdma'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$outputFile=Join-Path $buildDir 'fdma_write_tb.vvp'
$rtl=Join-Path $projectDir 'src/write_fdma'
& $Iverilog -g2012 -Wall -s fdma_write_tb -o $outputFile (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'uiFDMA.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'fdma_write_tb.sv')
if($LASTEXITCODE -ne 0){throw 'FDMA compilation failed'}
& $Vvp $outputFile
if($LASTEXITCODE -ne 0){throw 'FDMA simulation failed'}
$productionOutput=Join-Path $buildDir 'fdma_write_1080p_width_tb.vvp'
& $Iverilog -g2012 -Wall -s fdma_write_tb '-Pfdma_write_tb.XSIZE=480' -o $productionOutput (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'uiFDMA.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'fdma_write_tb.sv')
if($LASTEXITCODE -ne 0){throw 'Production-width FDMA compilation failed'}
& $Vvp $productionOutput
if($LASTEXITCODE -ne 0){throw 'Production-width FDMA simulation failed'}
$wrapOutput=Join-Path $buildDir 'fdma_write_pointer_wrap_tb.vvp'
& $Iverilog -g2012 -Wall -s fdma_write_tb '-Pfdma_write_tb.XSIZE=480' '-Pfdma_write_tb.YSIZE=20' '-Pfdma_write_tb.FIFO_DEPTH=4096' '-Pfdma_write_tb.PIXEL_HALF=17' -o $wrapOutput (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'uiFDMA.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'fdma_write_tb.sv')
if($LASTEXITCODE -ne 0){throw 'Production-depth pointer-wrap FDMA compilation failed'}
& $Vvp $wrapOutput
if($LASTEXITCODE -ne 0){throw 'Production-depth pointer-wrap FDMA simulation failed'}
& (Join-Path $PSScriptRoot 'run_capture_overflow.ps1') -Iverilog $Iverilog -Vvp $Vvp
if($LASTEXITCODE -ne 0){throw 'Capture overflow recovery regressions failed'}
