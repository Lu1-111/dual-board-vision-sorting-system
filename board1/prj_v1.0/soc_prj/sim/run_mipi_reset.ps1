param([string]$Iverilog='E:\Msys2\ucrt64\bin\iverilog.exe',
      [string]$Vvp='E:\Msys2\ucrt64\bin\vvp.exe')
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path $Iverilog -Parent)+';'+$env:PATH
$projectDir=Split-Path $PSScriptRoot -Parent
$buildDir=Join-Path $PSScriptRoot 'build-mipi-reset'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$output=Join-Path $buildDir 'mipi_reset.vvp'
& $Iverilog -g2012 -Wall -s mipi_reset_tb -o $output `
    (Join-Path $projectDir 'src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v') `
    (Join-Path $projectDir 'src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv') `
    (Join-Path $projectDir 'src/MIPI_V2/src/design_top_wrapper.v') `
    (Join-Path $PSScriptRoot 'mipi_reset_tb.sv')
if($LASTEXITCODE -ne 0){throw 'MIPI reset simulation compilation failed'}
& $Vvp $output
if($LASTEXITCODE -ne 0){throw 'MIPI reset simulation failed'}
