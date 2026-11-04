param([string]$Iverilog='E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp='E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path $Iverilog -Parent)+';'+$env:PATH
$projectDir=Split-Path $PSScriptRoot -Parent
$rtl=Join-Path $projectDir 'src/write_fdma'
$buildDir=Join-Path $PSScriptRoot 'build-fdma'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
foreach($configuration in @('small','production')) {
    $outputFile=Join-Path $buildDir "capture_overflow_$configuration.vvp"
    $parameters=@()
    if($configuration -eq 'production') {
        $parameters=@('-Pcapture_overflow_tb.XSIZE=480',
                      '-Pcapture_overflow_tb.YSIZE=12',
                      '-Pcapture_overflow_tb.FIFO_DEPTH=4096',
                      '-Pcapture_overflow_tb.XDIV=2')
    }
    & $Iverilog -g2012 -Wall -s capture_overflow_tb @parameters -o $outputFile (Join-Path $rtl 'uidbuf_write.v') (Join-Path $rtl 'uiFDMA.v') (Join-Path $rtl 'wfifo.v') (Join-Path $rtl 'fs_cap.v') (Join-Path $PSScriptRoot 'capture_overflow_tb.sv')
    if($LASTEXITCODE -ne 0){throw "Capture overflow $configuration compilation failed"}
    & $Vvp $outputFile
    if($LASTEXITCODE -ne 0){throw "Capture overflow $configuration simulation failed"}
}
